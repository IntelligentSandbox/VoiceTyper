#pragma once

#include "state.h"
#include "transcription_core.h"

#include "host_services.h"
#include "perf.h"
#include "stream_chunker.h"

#include <cstdio>
#include <cmath>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Platform audio capture interface
// ---------------------------------------------------------------------------
// bool platform_audio_capture(PlatformRuntimeState *Platform, GlobalState *AppState, int DeviceIndex)
//
// Platform-specific function that opens the audio capture device at the given
// index, captures PCM audio, converts to float samples, and appends them to
// AppState->AudioAccum (protected by AppState->AudioBufferMutex).
// Runs in a loop until AppState->CaptureRunning becomes false.
// Returns true on success, false if device setup fails.
//
// Implementations:
//   Win32 — platform_win32.h (uses WaveIn API)
// ---------------------------------------------------------------------------

static float
compute_rms(const float *Samples, int Count)
{
	if (Count <= 0) return 0.0f;

	double Sum = 0.0;
	for (int i = 0; i < Count; i++)
	{
		Sum += (double)Samples[i] * (double)Samples[i];
	}

	return (float)sqrt(Sum / (double)Count);
}

static bool
paste_override_name_matches(const std::string &A, const std::string &B)
{
	if (A.size() != B.size()) return false;
	for (size_t i = 0; i < A.size(); i++)
	{
		char Ca = (A[i] >= 'A' && A[i] <= 'Z') ? (char)(A[i] - 'A' + 'a') : A[i];
		char Cb = (B[i] >= 'A' && B[i] <= 'Z') ? (char)(B[i] - 'A' + 'a') : B[i];
		if (Ca != Cb) return false;
	}
	return true;
}

static HotkeyConfig
resolve_paste_hotkey(GlobalState *AppState, void *TargetWindow)
{
	HotkeyConfig PasteHotkey = AppState->PasteHotkey;
	if (!TargetWindow) return PasteHotkey;

	std::string ProcessName = platform_get_window_process_name(TargetWindow);
	if (ProcessName.empty()) return PasteHotkey;

	std::vector<PasteHotkeyOverride> Overrides;
	{
		std::lock_guard<std::mutex> Lock(AppState->PasteHotkeyOverridesMutex);
		Overrides = AppState->PasteHotkeyOverrides;
	}

	for (const PasteHotkeyOverride &Override : Overrides)
	{
		if (paste_override_name_matches(Override.ProcessName, ProcessName))
		{
			return Override.Hotkey;
		}
	}

	return PasteHotkey;
}

struct SttInferenceParams
{
	whisper_full_params WhisperParams;
	parakeet_full_params ParakeetParams;
};

static void
run_stt_on_chunk(GlobalState *AppState, SttInferenceParams &Params, const float *Samples, int SampleCount)
{
	float Rms = compute_rms(Samples, SampleCount);
	if (Rms < PIPELINE_SILENCE_RMS_THRESHOLD)
	{
		perf_event("transcribe_skipped_silence");
		return;
	}

	std::string Transcription;
	std::vector<TranscribedWord> TranscribedWords;
	std::chrono::steady_clock::time_point TxStart = std::chrono::steady_clock::now();
	PerfSpan TranscribeSpan("transcribe");
	int Ret;
	if (AppState->WhisperState.Kind == ENGINE_PARAKEET)
	{
		Ret = transcribe_pcm_to_string(
			AppState->WhisperState.ParakeetContext, Params.ParakeetParams, Samples, SampleCount,
			&Transcription, &TranscribedWords);
	}
	else
	{
		Ret = transcribe_pcm_to_string(
			AppState->WhisperState.WhisperContext, Params.WhisperParams, Samples, SampleCount,
			&Transcription, &TranscribedWords);
	}
	std::chrono::steady_clock::time_point TxEnd = std::chrono::steady_clock::now();
	double TxMs = std::chrono::duration<double, std::milli>(TxEnd - TxStart).count();
	if (Ret == 0) AppState->LastTranscriptionMs.store(TxMs);

	if (Ret != 0)
	{
		if (AppState->WhisperState.Kind == ENGINE_PARAKEET)
			printf("[audio_pipeline] parakeet_full failed (ret=%d)\n", Ret);
		else
			printf("[audio_pipeline] whisper_full failed (ret=%d)\n", Ret);
		return;
	}

	if (!Transcription.empty())
	{
		void *TargetWindow = platform_get_foreground_window(&AppState->Platform);
		if (TargetWindow == AppState->Platform.OwnWindow) TargetWindow = nullptr;
		if (!TargetWindow)
		{
			printf("[transcription] %s\n", Transcription.c_str());
			if (AppState->CopyToClipboardWhenNoTarget)
				platform_set_clipboard_text(&AppState->Platform, Transcription.c_str());
		}
		std::chrono::steady_clock::time_point PasteStart = std::chrono::steady_clock::now();
		HotkeyConfig PasteHotkey = resolve_paste_hotkey(AppState, TargetWindow);
		PerfSpan PasteSpan("paste");
		platform_inject_text(
			&AppState->Platform,
			TargetWindow,
			Transcription.c_str(),
			AppState->UseCharByCharInjection,
			PasteHotkey,
			AppState->PreserveClipboardOnPaste,
			AppState->ClipboardRestoreDelayMs);
		std::chrono::steady_clock::time_point PasteEnd = std::chrono::steady_clock::now();
		double PasteMs = std::chrono::duration<double, std::milli>(PasteEnd - PasteStart).count();
		AppState->LastPasteMs.store(PasteMs);

		{
			std::lock_guard<std::mutex> Lock(AppState->Ui.TranscribedTextMutex);
			AppState->Ui.TranscribedTextWords = TranscribedWords;
			AppState->Ui.TranscribedTextSerial += 1;
		}
	}
}

// ---------------------------------------------------------------------------
// Streaming pipeline  (continuous capture + silence-bounded inference)
// ---------------------------------------------------------------------------

struct StreamingChunkQueue
{
	std::mutex Mutex;
	std::condition_variable Condition;
	std::deque<AudioClip> Chunks;
	bool Closed;
};

static void
stream_push_completed_chunk(StreamingChunkQueue *Queue, AudioClip *Clip)
{
	if (Clip->TotalSamples <= 0) return;

	{
		std::lock_guard<std::mutex> Lock(Queue->Mutex);
		Queue->Chunks.push_back(*Clip);
	}
	*Clip = AudioClip{};

	Queue->Condition.notify_one();
}

static bool
stream_pop_completed_chunk(StreamingChunkQueue *Queue, AudioClip *Clip)
{
	std::unique_lock<std::mutex> Lock(Queue->Mutex);
	Queue->Condition.wait(Lock, [Queue]() {
		return Queue->Closed || !Queue->Chunks.empty();
	});

	if (Queue->Chunks.empty()) return false;

	*Clip = Queue->Chunks.front();
	Queue->Chunks.pop_front();
	return true;
}

static void
stream_close_completed_chunks(StreamingChunkQueue *Queue)
{
	{
		std::lock_guard<std::mutex> Lock(Queue->Mutex);
		Queue->Closed = true;
	}

	Queue->Condition.notify_all();
}

static void
stream_finish_buffer_on_stop(GlobalState *AppState, StreamingChunkQueue *Queue, bool HasSpeech)
{
	AudioClip Chunk;
	bool Finalize = false;

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		Finalize = AppState->StreamingFinalizeOnStop.load() &&
			HasSpeech &&
			clip_duration_ms(&AppState->AudioAccum) >= STREAM_MIN_CHUNK_DURATION_MS;
		Chunk = AppState->AudioAccum;
		AppState->AudioAccum = AudioClip{};
	}

	if (Finalize) stream_push_completed_chunk(Queue, &Chunk);
	else clip_release(&AppState->AudioPool, &Chunk);
}

static void
stream_segment_thread(GlobalState *AppState, StreamingChunkQueue *Queue)
{
	StreamSpeechDetector Detector;
	const int PollWindowSamples = AUDIO_CAPTURE_SAMPLE_RATE * STREAM_POLL_INTERVAL_MS / 1000;

	while (AppState->CaptureRunning.load())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(STREAM_POLL_INTERVAL_MS));
		if (!AppState->CaptureRunning.load()) break;

		AudioClip Chunk;
		AudioClip Discard;
		bool ShouldCut = false;
		{
			std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
			int BufferSize = AppState->AudioAccum.TotalSamples;
			if (BufferSize == 0) continue;

			float Recent[PollWindowSamples];
			int RecentCount = clip_read_last_n(&AppState->AudioAccum, Recent, PollWindowSamples);
			float CurrentRms = compute_rms(Recent, RecentCount);

			bool IsSpeech = stream_speech_detector_poll(&Detector, CurrentRms, STREAM_POLL_INTERVAL_MS);
			if (!IsSpeech && !Detector.HasSpeech)
			{
				Discard = AppState->AudioAccum;
				AppState->AudioAccum = AudioClip{};
			}
			else
			{
				int BufferDurationMs = (int)((int64_t)BufferSize * 1000 / AUDIO_CAPTURE_SAMPLE_RATE);
				ShouldCut = Detector.HasSpeech &&
					Detector.SilenceMs >= STREAM_SILENCE_DURATION_MS &&
					BufferDurationMs >= STREAM_MIN_CHUNK_DURATION_MS;
				if (ShouldCut)
				{
					Chunk = AppState->AudioAccum;
					AppState->AudioAccum = AudioClip{};
					Detector.SilenceMs = 0;
					Detector.HasSpeech = false;
				}
			}
		}

		clip_release(&AppState->AudioPool, &Discard);

		if (ShouldCut) stream_push_completed_chunk(Queue, &Chunk);
	}

	stream_finish_buffer_on_stop(AppState, Queue, Detector.HasSpeech);
}

static void
stream_infer_thread(GlobalState *AppState, StreamingChunkQueue *Queue)
{
	std::string InitialPrompt;
	{
		std::lock_guard<std::mutex> Lock(AppState->WhisperInitialPromptMutex);
		InitialPrompt = AppState->WhisperInitialPrompt;
	}

	SttInferenceParams Params = {};
	Params.WhisperParams = make_transcription_whisper_params(
		AppState->WhisperThreadCount,
		STREAMING_WHISPER_VAD,
		AppState->VadModelPath.c_str(),
		InitialPrompt.empty() ? nullptr : InitialPrompt.c_str());
	Params.WhisperParams.single_segment = true;
	Params.ParakeetParams = make_transcription_parakeet_params(AppState->WhisperThreadCount);

	for (;;)
	{
		AudioClip Clip;
		if (!stream_pop_completed_chunk(Queue, &Clip)) break;

		int SampleCount = staging_gather(&AppState->WhisperStaging, &Clip);
		clip_release(&AppState->AudioPool, &Clip);
		if (SampleCount <= 0) continue;

		run_stt_on_chunk(AppState, Params, AppState->WhisperStaging.data(), SampleCount);
	}
}

static void
streaming_pipeline_thread(GlobalState *AppState, int DeviceIndex)
{
	StreamingChunkQueue ChunkQueue = {};
	std::thread SegmentThread(stream_segment_thread, AppState, &ChunkQueue);
	std::thread InferThread(stream_infer_thread, AppState, &ChunkQueue);
	platform_audio_capture(&AppState->Platform, AppState, DeviceIndex);
	AppState->CaptureRunning.store(false);
	SegmentThread.join();
	stream_close_completed_chunks(&ChunkQueue);
	InferThread.join();
	perf_event("stream_pipeline_done");
	AppState->PipelineActive.store(false);
	AppState->StreamingFinalizeOnStop.store(false);
}

// ---------------------------------------------------------------------------
// Record pipeline  (capture everything, single whisper_full on stop)
// ---------------------------------------------------------------------------

static void
record_pipeline_thread(GlobalState *AppState, int DeviceIndex)
{
	platform_audio_capture(&AppState->Platform, AppState, DeviceIndex);

	bool Cancelled = AppState->CancelRequested.load();
	AppState->CancelRequested.store(false);

	// Capture has stopped — drain whatever is in the accumulator, gather it
	// into the staging buffer (releasing the blocks first on cancel) and
	// transcribe once.
	AudioClip Clip;
	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		Clip = AppState->AudioAccum;
		AppState->AudioAccum = AudioClip{};
	}

	int SampleCount = 0;
	if (!Cancelled)
	{
		SampleCount = staging_gather(&AppState->WhisperStaging, &Clip);
	}
	clip_release(&AppState->AudioPool, &Clip);

	if (!Cancelled && SampleCount > 0)
	{
		std::string InitialPrompt;
		{
			std::lock_guard<std::mutex> Lock(AppState->WhisperInitialPromptMutex);
			InitialPrompt = AppState->WhisperInitialPrompt;
		}

		SttInferenceParams Params = {};
		Params.WhisperParams = make_transcription_whisper_params(
			AppState->WhisperThreadCount,
			RECORD_WHISPER_VAD,
			AppState->VadModelPath.c_str(),
			InitialPrompt.empty() ? nullptr : InitialPrompt.c_str());
		Params.WhisperParams.single_segment = false;
		Params.ParakeetParams = make_transcription_parakeet_params(AppState->WhisperThreadCount);

		run_stt_on_chunk(AppState, Params, AppState->WhisperStaging.data(), SampleCount);
	}

	perf_event("record_pipeline_done");
	AppState->PipelineActive.store(false);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static bool
pipeline_preflight(GlobalState *AppState)
{
	if (!is_stt_model_loaded(&AppState->WhisperState)) return false;

	int DeviceIndex = AppState->CurrentAudioDeviceIndex;
	if (DeviceIndex < 0 || DeviceIndex >= (int)AppState->AudioInputDevices.size()) return false;

	return true;
}

inline bool
start_record_pipeline(GlobalState *AppState)
{
	AppState->PipelineRequestNs.store(perf_now_ns());
	perf_event("record_start_requested");

	if (!pipeline_preflight(AppState)) return false;

	if (AppState->CaptureThread.joinable()) AppState->CaptureThread.join();

	int DeviceIndex = AppState->CurrentAudioDeviceIndex;

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		clip_release(&AppState->AudioPool, &AppState->AudioAccum);
	}

	AppState->CaptureRunning.store(true);
	AppState->PipelineActive.store(true);
	AppState->CaptureThread = std::thread(record_pipeline_thread, AppState, DeviceIndex);

	return true;
}

// Signal the record capture to stop (non-blocking). The background thread will
// finish transcription and restore the button itself via invokeMethod.
inline void
signal_record_stop(GlobalState *AppState)
{
	AppState->CaptureRunning.store(false);
}

// TODO(warren): kinda janky still.
inline bool
start_streaming_pipeline(GlobalState *AppState)
{
	AppState->PipelineRequestNs.store(perf_now_ns());
	perf_event("stream_start_requested");

	if (!pipeline_preflight(AppState)) return false;

	if (AppState->CaptureThread.joinable()) AppState->CaptureThread.join();

	int DeviceIndex = AppState->CurrentAudioDeviceIndex;

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		clip_release(&AppState->AudioPool, &AppState->AudioAccum);
	}

	AppState->CaptureRunning.store(true);
	AppState->StreamingFinalizeOnStop.store(false);
	AppState->PipelineActive.store(true);
	AppState->CaptureThread = std::thread(streaming_pipeline_thread, AppState, DeviceIndex);

	return true;
}

inline void
stop_streaming_pipeline(GlobalState *AppState, bool FinalizeCurrentChunk = false)
{
	if (!AppState->CaptureRunning.load()) return;

	AppState->StreamingFinalizeOnStop.store(FinalizeCurrentChunk);
	AppState->CaptureRunning.store(false);
}
