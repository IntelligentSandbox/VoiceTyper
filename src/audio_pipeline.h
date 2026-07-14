#pragma once

#include "state.h"
#include "transcription_core.h"

#include "host_services.h"
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

static void
run_whisper_on_chunk(GlobalState *AppState, whisper_full_params &Params, std::vector<float> &Chunk)
{
	float Rms = compute_rms(Chunk.data(), (int)Chunk.size());
	if (Rms < PIPELINE_SILENCE_RMS_THRESHOLD)
		return;

	std::string Transcription;
	int Ret = transcribe_pcm_to_string(
		AppState->WhisperState.Context, Params, Chunk.data(), (int)Chunk.size(), &Transcription);

	if (Ret != 0)
	{
		printf("[audio_pipeline] whisper_full failed (ret=%d)\n", Ret);
		return;
	}

	if (!Transcription.empty())
	{
		void *TargetWindow = platform_get_foreground_window(&AppState->Platform);
		if (TargetWindow == AppState->Platform.OwnWindow) TargetWindow = nullptr;
		if (!TargetWindow)
			printf("[transcription] %s\n", Transcription.c_str());
		platform_inject_text(
			&AppState->Platform,
			TargetWindow,
			Transcription.c_str(),
			AppState->UseCharByCharInjection);
	}
}

// ---------------------------------------------------------------------------
// Streaming pipeline  (continuous capture + silence-bounded inference)
// ---------------------------------------------------------------------------

struct StreamingChunkQueue
{
	std::mutex Mutex;
	std::condition_variable Condition;
	std::deque<std::vector<float>> Chunks;
	bool Closed;
};

static void
stream_push_completed_chunk(StreamingChunkQueue *Queue, std::vector<float> &Chunk)
{
	if (Chunk.empty()) return;

	{
		std::lock_guard<std::mutex> Lock(Queue->Mutex);
		Queue->Chunks.push_back(std::move(Chunk));
	}

	Queue->Condition.notify_one();
}

static bool
stream_pop_completed_chunk(StreamingChunkQueue *Queue, std::vector<float> *Chunk)
{
	std::unique_lock<std::mutex> Lock(Queue->Mutex);
	Queue->Condition.wait(Lock, [Queue]() {
		return Queue->Closed || !Queue->Chunks.empty();
	});

	if (Queue->Chunks.empty())
		return false;

	*Chunk = std::move(Queue->Chunks.front());
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
	std::vector<float> Chunk;

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		int BufferSize = (int)AppState->AudioAccumBuffer.size();
		int BufferDurationMs = BufferSize * 1000 / AUDIO_CAPTURE_SAMPLE_RATE;

		if (AppState->StreamingFinalizeOnStop.load() &&
			HasSpeech &&
			BufferDurationMs >= STREAM_MIN_CHUNK_DURATION_MS)
		{
			Chunk = std::move(AppState->AudioAccumBuffer);
		}

		AppState->AudioAccumBuffer.clear();
	}

	stream_push_completed_chunk(Queue, Chunk);
}

static void
stream_segment_thread(GlobalState *AppState, StreamingChunkQueue *Queue)
{
	int SilenceMs = 0;
	bool HasSpeech = false;

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
			{
				HasSpeech = true;
				SilenceMs = 0;
			}
			else if (HasSpeech)
			{
				SilenceMs += STREAM_POLL_INTERVAL_MS;
			}
			else
			{
				AppState->AudioAccumBuffer.clear();
				continue;
			}

			int BufferDurationMs = BufferSize * 1000 / AUDIO_CAPTURE_SAMPLE_RATE;
			bool ShouldCut = false;

			if (HasSpeech &&
				SilenceMs >= STREAM_SILENCE_DURATION_MS &&
				BufferDurationMs >= STREAM_MIN_CHUNK_DURATION_MS)
			{
				ShouldCut = true;
			}

			if (!ShouldCut) continue;

			Chunk = std::move(AppState->AudioAccumBuffer);
			AppState->AudioAccumBuffer.clear();
			SilenceMs = 0;
			HasSpeech = false;
		}

		stream_push_completed_chunk(Queue, Chunk);
	}

	stream_finish_buffer_on_stop(AppState, Queue, HasSpeech);
}

static void
stream_infer_thread(GlobalState *AppState, StreamingChunkQueue *Queue)
{
	whisper_full_params Params = make_transcription_whisper_params(
		AppState->WhisperThreadCount,
		VOICETYPER_STREAMING_WHISPER_VAD != 0,
		AppState->VadModelPath.c_str());
	Params.single_segment      = true;

	for (;;)
	{
		std::vector<float> Chunk;
		if (!stream_pop_completed_chunk(Queue, &Chunk))
			break;

		run_whisper_on_chunk(AppState, Params, Chunk);
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

	// Capture has stopped — drain whatever is in the buffer and transcribe once.
	std::vector<float> Chunk;
	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		if (Cancelled)
		{
			AppState->AudioAccumBuffer.clear();
		}
		else
		{
			Chunk = std::move(AppState->AudioAccumBuffer);
			AppState->AudioAccumBuffer.clear();
		}
	}

	if (!Cancelled && !Chunk.empty())
	{
		whisper_full_params Params = make_transcription_whisper_params(
			AppState->WhisperThreadCount,
			VOICETYPER_RECORD_WHISPER_VAD != 0,
			AppState->VadModelPath.c_str());
		Params.single_segment      = false;

		run_whisper_on_chunk(AppState, Params, Chunk);
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
		return false;

	int DeviceIndex = AppState->CurrentAudioDeviceIndex;
	if (DeviceIndex < 0 || DeviceIndex >= (int)AppState->AudioInputDevices.size())
		return false;

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

	return true;
}

// Signal the record capture to stop (non-blocking). The background thread will
// finish transcription and restore the button itself via invokeMethod.
inline
void
signal_record_stop(GlobalState *AppState)
{
	AppState->CaptureRunning.store(false);
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
	AppState->StreamingFinalizeOnStop.store(false);
	AppState->PipelineActive.store(true);
	AppState->CaptureThread = std::thread(streaming_pipeline_thread, AppState, DeviceIndex);

	return true;
}

inline
void
stop_streaming_pipeline(GlobalState *AppState, bool FinalizeCurrentChunk = false)
{
	if (!AppState->CaptureRunning.load() && !AppState->CaptureThread.joinable()) return;

	AppState->StreamingFinalizeOnStop.store(FinalizeCurrentChunk);
	AppState->CaptureRunning.store(false);

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	AppState->PipelineActive.store(false);
	AppState->StreamingFinalizeOnStop.store(false);
}
