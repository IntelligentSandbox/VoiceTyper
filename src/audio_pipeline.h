#pragma once

#include "state.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

#include <windows.h>
#include <mmsystem.h>

#define VAD_MODEL_PATH "models/ggml-silero-v5.1.2.bin"

// Streaming pipeline: how many samples to accumulate before each inference pass.
// 16000 samples/sec * 3 sec = 48000 samples.
#define PIPELINE_STREAM_CHUNK_SAMPLES (AUDIO_CAPTURE_SAMPLE_RATE * 3)

// Minimum RMS energy to bother sending a chunk to whisper (streaming mode).
#define PIPELINE_SILENCE_RMS_THRESHOLD 0.002f

// ---------------------------------------------------------------------------
// Shared internals
// ---------------------------------------------------------------------------

// TODO(warren): These data structures are WIN32 dependent right now.
// We should break it into platform agnostic, and any WIN32 stuff should be in platform_win32.h
// Details not sure yet, but the agreement is that we definitely pass std::vector<int16_t> audio data.
// Win32 specific data structures...idk put where or hide inside of what functions, yet.
struct WaveInBuffer
{
	WAVEHDR Header;
	std::vector<int16_t> Data;
};

struct AudioPipelineContext
{
	HWAVEIN WaveInHandle;
	HANDLE  ReadyEvent;
	std::vector<WaveInBuffer> Buffers;
	std::atomic<bool> *Running;
};

static void CALLBACK
wavein_proc(
	HWAVEIN   hWaveIn,
	UINT      uMsg,
	DWORD_PTR dwInstance,
	DWORD_PTR dwParam1,
	DWORD_PTR dwParam2)
{
	if (uMsg != WIM_DATA) return;

	AudioPipelineContext *Ctx = reinterpret_cast<AudioPipelineContext*>(dwInstance);
	SetEvent(Ctx->ReadyEvent);
}

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

// TODO(warren): This function uses WIN32 API.
// We want to keep this function interface platform agnostic, which it is right now. But the internals
// are platform dependent. Thinking, should just put individual platforms code inside this func
// and use preprocessor macros to separate what actually gets compiled, or duplicate this
// function definition in separate platform specific files with same interface?
//
// Opens the WaveIn device, queues buffers, starts capture, and pumps completed
// buffers into AppState->AudioAccumBuffer until CaptureRunning goes false.
// Caller is responsible for setting CaptureRunning before calling.
static bool
run_wavein_capture(GlobalState *AppState, int DeviceIndex)
{
	const int SamplesPerBuffer = (AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_CAPTURE_BUFFER_MS) / 1000;

	AudioPipelineContext PipeCtx = {};
	PipeCtx.Running = &AppState->CaptureRunning;

	PipeCtx.ReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!PipeCtx.ReadyEvent)
	{
		printf("[audio_pipeline] ERROR: Failed to create ready event\n");
		return false;
	}

	WAVEFORMATEX Format       = {};
	Format.wFormatTag         = WAVE_FORMAT_PCM;
	Format.nChannels          = AUDIO_CAPTURE_CHANNELS;
	Format.nSamplesPerSec     = AUDIO_CAPTURE_SAMPLE_RATE;
	Format.wBitsPerSample     = AUDIO_CAPTURE_BITS_PER_SAMPLE;
	Format.nBlockAlign        = (Format.nChannels * Format.wBitsPerSample) / 8;
	Format.nAvgBytesPerSec    = Format.nSamplesPerSec * Format.nBlockAlign;
	Format.cbSize             = 0;

	MMRESULT Res = waveInOpen(
		&PipeCtx.WaveInHandle,
		(UINT)DeviceIndex,
		&Format,
		(DWORD_PTR)wavein_proc,
		(DWORD_PTR)&PipeCtx,
		CALLBACK_FUNCTION);

	if (Res != MMSYSERR_NOERROR)
	{
		printf("[audio_pipeline] ERROR: waveInOpen failed (mmresult=%u)\n", Res);
		CloseHandle(PipeCtx.ReadyEvent);
		return false;
	}

	PipeCtx.Buffers.resize(AUDIO_CAPTURE_BUFFER_COUNT);
	for (int i = 0; i < AUDIO_CAPTURE_BUFFER_COUNT; i++)
	{
		WaveInBuffer &Buf         = PipeCtx.Buffers[i];
		Buf.Data.resize(SamplesPerBuffer);
		memset(&Buf.Header, 0, sizeof(WAVEHDR));
		Buf.Header.lpData         = reinterpret_cast<LPSTR>(Buf.Data.data());
		Buf.Header.dwBufferLength = (DWORD)(SamplesPerBuffer * sizeof(int16_t));

		waveInPrepareHeader(PipeCtx.WaveInHandle, &Buf.Header, sizeof(WAVEHDR));
		waveInAddBuffer(PipeCtx.WaveInHandle, &Buf.Header, sizeof(WAVEHDR));
	}

	waveInStart(PipeCtx.WaveInHandle);
	#ifdef DEBUG
		printf("[audio_pipeline] Capture started on device index %d\n", DeviceIndex);
	#endif

	while (AppState->CaptureRunning.load())
	{
		DWORD WaitResult = WaitForSingleObject(PipeCtx.ReadyEvent, 50);
		if (WaitResult == WAIT_TIMEOUT) continue;

		for (int i = 0; i < AUDIO_CAPTURE_BUFFER_COUNT; i++)
		{
			WAVEHDR &Hdr = PipeCtx.Buffers[i].Header;
			if (!(Hdr.dwFlags & WHDR_DONE)) continue;

			int SamplesGot = (int)(Hdr.dwBytesRecorded / sizeof(int16_t));
			if (SamplesGot > 0)
			{
				const int16_t *Src = PipeCtx.Buffers[i].Data.data();
				std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
				size_t OldSize = AppState->AudioAccumBuffer.size();
				AppState->AudioAccumBuffer.resize(OldSize + SamplesGot);
				for (int j = 0; j < SamplesGot; j++)
				{
					AppState->AudioAccumBuffer[OldSize + j] = Src[j] / 32768.0f;
				}
			}

			Hdr.dwFlags         = 0;
			Hdr.dwBytesRecorded = 0;
			waveInPrepareHeader(PipeCtx.WaveInHandle, &Hdr, sizeof(WAVEHDR));
			waveInAddBuffer(PipeCtx.WaveInHandle, &Hdr, sizeof(WAVEHDR));
		}
	}

	waveInStop(PipeCtx.WaveInHandle);
	waveInReset(PipeCtx.WaveInHandle);

	for (int i = 0; i < AUDIO_CAPTURE_BUFFER_COUNT; i++)
	{
		waveInUnprepareHeader(PipeCtx.WaveInHandle, &PipeCtx.Buffers[i].Header, sizeof(WAVEHDR));
	}

	waveInClose(PipeCtx.WaveInHandle);
	CloseHandle(PipeCtx.ReadyEvent);

	#ifdef DEBUG
		printf("[audio_pipeline] Capture stopped\n");
	#endif
	return true;
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
		HWND TargetWindow = GetForegroundWindow();
		if (TargetWindow == AppState->OwnWindow) TargetWindow = nullptr;
		#ifdef DEBUG
			printf("[transcription] %s\n", Transcription.c_str());
		#else
			if (!TargetWindow)
				printf("[transcription] %s\n", Transcription.c_str());
		#endif
		#ifdef _WIN32
			inject_text_to_window(TargetWindow, Transcription.c_str());
		#endif
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

	#ifdef DEBUG
		printf("[audio_pipeline] Stream inference thread started\n");
	#endif

	while (AppState->CaptureRunning.load())
	{
		Sleep(3000);

		if (!AppState->CaptureRunning.load())
		{
			break;
		}

		std::vector<float> Chunk;
		{
			std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
			if ((int)AppState->AudioAccumBuffer.size() < PIPELINE_STREAM_CHUNK_SAMPLES)
			{
				continue;
			}

			Chunk = std::move(AppState->AudioAccumBuffer);
			AppState->AudioAccumBuffer.clear();
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
	run_wavein_capture(AppState, DeviceIndex);
	InferThread.join();
}

// ---------------------------------------------------------------------------
// Record pipeline  (capture everything, single whisper_full on stop)
// ---------------------------------------------------------------------------

static void
record_pipeline_thread(GlobalState *AppState, int DeviceIndex)
{
	run_wavein_capture(AppState, DeviceIndex);

	// Capture has stopped — drain whatever is in the buffer and transcribe once.
	std::vector<float> Chunk;
	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		Chunk = std::move(AppState->AudioAccumBuffer);
		AppState->AudioAccumBuffer.clear();
	}

	if (!Chunk.empty())
	{
		whisper_full_params Params = make_whisper_params(AppState);
		Params.single_segment      = false;

		#ifdef DEBUG
			printf("[audio_pipeline] Record: transcribing %d samples...\n", (int)Chunk.size());
		#endif
		run_whisper_on_chunk(AppState, Params, Chunk);
	}
	else
	{
		#ifdef DEBUG
			printf("[audio_pipeline] Record: no audio captured\n");
		#endif
	}

	// Post back to the main thread to restore the record button to idle state.
	QMetaObject::invokeMethod(
		AppState->QtApp,
		[AppState]()
		{
			AppState->IsRecording = false;
			AppState->RecordButton->setEnabled(true);
			AppState->RecordButton->setStyleSheet(BUTTON_STYLE_GREEN);
			AppState->RecordButton->setText(record_button_idle_label(AppState));
			AppState->StreamButton->setEnabled(true);
			AppState->StreamButton->setStyleSheet(BUTTON_STYLE_GREEN);
		},
		Qt::QueuedConnection);
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

	int DeviceIndex = AppState->CurrentAudioDeviceIndex;

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		AppState->AudioAccumBuffer.clear();
	}

	AppState->CaptureRunning.store(true);

	// Detach — the thread posts back to the main thread when transcription is done.
	std::thread(record_pipeline_thread, AppState, DeviceIndex).detach();

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

	int DeviceIndex = AppState->CurrentAudioDeviceIndex;

	{
		std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
		AppState->AudioAccumBuffer.clear();
	}

	AppState->CaptureRunning.store(true);
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

	#ifdef DEBUG
		printf("[audio_pipeline] Streaming pipeline stopped\n");
	#endif
}
