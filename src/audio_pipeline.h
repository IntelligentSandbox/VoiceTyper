#pragma once

#include "state.h"

#include "platform.h"

#include <cstdio>
#include <cmath>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

#define VAD_MODEL_PATH "vad_models/ggml-silero-v5.1.2.bin"

// Minimum RMS energy to bother sending a chunk to whisper.
#define PIPELINE_SILENCE_RMS_THRESHOLD 0.002f

// How often (ms) the stream inference thread polls the audio buffer for energy levels.
#define STREAM_POLL_INTERVAL_MS 100

// RMS energy threshold for classifying a poll interval as speech vs silence.
#define STREAM_SPEECH_RMS_THRESHOLD 0.002f

// Minimum chunk duration (ms) before a speech→silence transition can trigger a cutoff.
#define STREAM_MIN_CHUNK_DURATION_MS 1000

// Maximum chunk duration (ms); forces a cutoff even during continuous speech.
#define STREAM_MAX_CHUNK_DURATION_MS 10000

// How long silence (ms) must persist after speech before cutting the chunk.
#define STREAM_SILENCE_DURATION_MS 500

// ---------------------------------------------------------------------------
// Platform audio capture interface
// ---------------------------------------------------------------------------
// bool platform_audio_capture(GlobalState *AppState, int DeviceIndex)
//
// Platform-specific function that opens the audio capture device at the given
// index, captures PCM audio, converts to float samples, and appends them to
// AppState->AudioAccumBuffer (protected by AppState->AudioBufferMutex).
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

static whisper_full_params
make_whisper_params(GlobalState *AppState)
{
	whisper_full_params Params  = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
	Params.language             = "en";
	Params.translate            = false;
	Params.no_context           = true;
	Params.print_progress       = false;
	Params.print_realtime       = false;
	Params.print_special        = false;
	Params.print_timestamps     = false;
	Params.n_threads            = AppState->WhisperThreadCount;
	Params.vad                  = true;
	Params.vad_model_path       = VAD_MODEL_PATH;

	whisper_vad_params VadParams      = whisper_vad_default_params();
	VadParams.threshold               = 0.5f;
	VadParams.min_speech_duration_ms  = 250;
	VadParams.min_silence_duration_ms = 500;
	Params.vad_params                 = VadParams;

	return Params;
}

static void
run_whisper_on_chunk(GlobalState *AppState, whisper_full_params &Params, std::vector<float> &Chunk)
{
	float Rms = compute_rms(Chunk.data(), (int)Chunk.size());
	if (Rms < PIPELINE_SILENCE_RMS_THRESHOLD)
	{
		#ifdef DEBUG
			printf("[audio_pipeline] Chunk silent (rms=%.5f), skipping\n", Rms);
		#endif
		return;
	}

	#ifdef DEBUG
		printf("[audio_pipeline] Running inference on %d samples (rms=%.5f)...\n",
			(int)Chunk.size(), Rms);
	#endif

	int Ret = whisper_full(
		AppState->WhisperState.Context,
		Params,
		Chunk.data(),
		(int)Chunk.size());

	if (Ret != 0)
	{
		printf("[audio_pipeline] whisper_full failed (ret=%d)\n", Ret);
		return;
	}

	int NumSegments = whisper_full_n_segments(AppState->WhisperState.Context);
	std::string Transcription;
	for (int i = 0; i < NumSegments; i++)
	{
		const char *Text = whisper_full_get_segment_text(AppState->WhisperState.Context, i);
		if (Text && Text[0] != '\0')
		{
			Transcription += Text;
		}
	}

	if (!Transcription.empty())
	{
		void *TargetWindow = platform_get_foreground_window();
		if (TargetWindow == AppState->OwnWindow) TargetWindow = nullptr;
		#ifdef DEBUG
			printf("[transcription] %s\n", Transcription.c_str());
		#else
			if (!TargetWindow)
				printf("[transcription] %s\n", Transcription.c_str());
		#endif
		platform_inject_text(TargetWindow, Transcription.c_str(), AppState->UseCharByCharInjection);
	}
}

// ---------------------------------------------------------------------------
// Streaming pipeline  (continuous capture + inference every N seconds)
// ---------------------------------------------------------------------------

static void
stream_infer_thread(GlobalState *AppState)
{
	whisper_full_params Params = make_whisper_params(AppState);
	Params.single_segment      = true;

	int SilenceMs = 0;

	#ifdef DEBUG
		printf("[audio_pipeline] Stream inference thread started\n");
	#endif

	while (AppState->CaptureRunning.load())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(STREAM_POLL_INTERVAL_MS));
		if (!AppState->CaptureRunning.load()) break;

		std::vector<float> Chunk;
		{
			std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
			int BufferSize = (int)AppState->AudioAccumBuffer.size();
			if (BufferSize == 0) continue;

			int RecentCount = AUDIO_CAPTURE_SAMPLE_RATE * STREAM_POLL_INTERVAL_MS / 1000;
			if (RecentCount > BufferSize) RecentCount = BufferSize;
			float CurrentRms = compute_rms(
				AppState->AudioAccumBuffer.data() + BufferSize - RecentCount, RecentCount);

			if (CurrentRms >= STREAM_SPEECH_RMS_THRESHOLD)
				SilenceMs = 0;
			else
				SilenceMs += STREAM_POLL_INTERVAL_MS;

			int BufferDurationMs = BufferSize * 1000 / AUDIO_CAPTURE_SAMPLE_RATE;
			bool ShouldCut = false;

			if (SilenceMs >= STREAM_SILENCE_DURATION_MS &&
				BufferDurationMs >= STREAM_MIN_CHUNK_DURATION_MS)
			{
				ShouldCut = true;
			}

			if (BufferDurationMs >= STREAM_MAX_CHUNK_DURATION_MS)
				ShouldCut = true;

			if (!ShouldCut) continue;

			Chunk = std::move(AppState->AudioAccumBuffer);
			AppState->AudioAccumBuffer.clear();
			SilenceMs = 0;
		}

		run_whisper_on_chunk(AppState, Params, Chunk);
	}

	#ifdef DEBUG
		printf("[audio_pipeline] Stream inference thread stopped\n");
	#endif
}

static void
streaming_pipeline_thread(GlobalState *AppState, int DeviceIndex)
{
	std::thread InferThread(stream_infer_thread, AppState);
	platform_audio_capture(AppState, DeviceIndex);
	InferThread.join();
}

// ---------------------------------------------------------------------------
// Record pipeline  (capture everything, single whisper_full on stop)
// ---------------------------------------------------------------------------

static void
record_pipeline_thread(GlobalState *AppState, int DeviceIndex)
{
	platform_audio_capture(AppState, DeviceIndex);

	bool Cancelled = AppState->CancelRequested.load();
	AppState->CancelRequested.store(false);

	// Capture has stopped — drain whatever is in the buffer and transcribe once.
	std::vector<float> Chunk;
	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		if (Cancelled)
		{
			AppState->AudioAccumBuffer.clear();
			#ifdef DEBUG
				printf("[audio_pipeline] Record: cancelled, discarding audio\n");
			#endif
		}
		else
		{
			Chunk = std::move(AppState->AudioAccumBuffer);
			AppState->AudioAccumBuffer.clear();
		}
	}

	if (!Cancelled && !Chunk.empty())
	{
		whisper_full_params Params = make_whisper_params(AppState);
		Params.single_segment      = false;

		#ifdef DEBUG
			printf("[audio_pipeline] Record: transcribing %d samples...\n", (int)Chunk.size());
		#endif
		run_whisper_on_chunk(AppState, Params, Chunk);
	}
	else if (!Cancelled)
	{
		#ifdef DEBUG
			printf("[audio_pipeline] Record: no audio captured\n");
		#endif
	}

	AppState->PipelineActive.store(false);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static bool
pipeline_preflight(GlobalState *AppState)
{
	if (!is_whisper_model_loaded(&AppState->WhisperState))
	{
		#ifdef DEBUG
			printf("[audio_pipeline] Cannot start: no model loaded\n");
		#endif
		return false;
	}

	int DeviceIndex = AppState->CurrentAudioDeviceIndex;
	if (DeviceIndex < 0 || DeviceIndex >= (int)AppState->AudioInputDevices.size())
	{
		#ifdef DEBUG
			printf("[audio_pipeline] Cannot start: invalid device index %d\n", DeviceIndex);
		#endif
		return false;
	}

	return true;
}

inline
bool
start_record_pipeline(GlobalState *AppState)
{
	if (!pipeline_preflight(AppState)) return false;

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	int DeviceIndex = AppState->CurrentAudioDeviceIndex;

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		AppState->AudioAccumBuffer.clear();
	}

	AppState->CaptureRunning.store(true);
	AppState->PipelineActive.store(true);
	AppState->CaptureThread = std::thread(record_pipeline_thread, AppState, DeviceIndex);

	#ifdef DEBUG
		printf("[audio_pipeline] Record pipeline started\n");
	#endif
	return true;
}

// Signal the record capture to stop (non-blocking). The background thread will
// finish transcription and restore the button itself via invokeMethod.
inline
void
signal_record_stop(GlobalState *AppState)
{
	AppState->CaptureRunning.store(false);
	#ifdef DEBUG
		printf("[audio_pipeline] Record capture stop signalled\n");
	#endif
}

// TODO(warren): kinda janky still.
inline
bool
start_streaming_pipeline(GlobalState *AppState)
{
	if (!pipeline_preflight(AppState)) return false;

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	int DeviceIndex = AppState->CurrentAudioDeviceIndex;

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		AppState->AudioAccumBuffer.clear();
	}

	AppState->CaptureRunning.store(true);
	AppState->PipelineActive.store(true);
	AppState->CaptureThread = std::thread(streaming_pipeline_thread, AppState, DeviceIndex);

	#ifdef DEBUG
		printf("[audio_pipeline] Streaming pipeline started\n");
	#endif
	return true;
}

inline
void
stop_streaming_pipeline(GlobalState *AppState)
{
	if (!AppState->CaptureRunning.load()) return;

	AppState->CaptureRunning.store(false);

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	AppState->PipelineActive.store(false);

	#ifdef DEBUG
		printf("[audio_pipeline] Streaming pipeline stopped\n");
	#endif
}
