#pragma once

#include "state.h"
#include "audio_pipeline.h"
#include "settings.h"
#include "sounds.h"

inline
void
update_audio_input_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentAudioDeviceIndex = Index;
	#ifdef DEBUG
		printf("Selected audio input device index: %d\n", Index);
		if (Index >= 0 && Index < (int)AppState->AudioInputDevices.size())
		{
			AudioInputDeviceInfo Device = AppState->AudioInputDevices.at(Index);
			printf("Selected audio input device: %s\n", Device.Name.c_str());
		}
	#endif
}

inline
void
update_inference_device_selection(GlobalState *AppState, int Index)
{
	int PreviousIndex = AppState->CurrentInferenceDeviceIndex;
	AppState->CurrentInferenceDeviceIndex = Index;

	#ifdef DEBUG
		printf("Selected inference device index: %d\n", Index);
	#endif

	if (Index >= 0 && Index < (int)AppState->InferenceDevices.size())
		save_string_setting("inference_device", AppState->InferenceDevices[Index].c_str());

	if (PreviousIndex == Index) return;
	if (!is_whisper_model_loaded(&AppState->WhisperState)) return;

	#ifdef DEBUG
		printf("[control] Inference device changed while model loaded (%d -> %d), reloading\n",
			PreviousIndex, Index);
	#endif

	bool Success = load_whisper_model(
		&AppState->WhisperState, AppState->WhisperState.LoadedModelIndex, Index);

	#ifdef DEBUG
		if (Success)
			printf("[control] Model reloaded on new inference device successfully\n");
		else
			printf("[control] ERROR: Failed to reload model on new inference device\n");
	#endif
}

inline
void
update_whisper_thread_count(GlobalState *AppState, int Count)
{
	if (Count < 1) Count = 1;
	AppState->WhisperThreadCount = Count;
	#ifdef DEBUG
		printf("[control] Whisper thread count set to: %d\n", Count);
	#endif
}

inline
void
update_stt_model_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentSTTModelIndex = Index;

	#ifdef DEBUG
		printf("[control] Selected STT model index: %d\n", Index);
	#endif

	if (!is_whisper_model_loaded(&AppState->WhisperState)) return;
	if (AppState->WhisperState.LoadedModelIndex == Index) return;

	#ifdef DEBUG
		printf("[control] Model index changed while loaded (%d -> %d), reloading\n",
			AppState->WhisperState.LoadedModelIndex, Index);
	#endif

	bool Success = load_whisper_model(
		&AppState->WhisperState, Index, AppState->CurrentInferenceDeviceIndex);

	#ifdef DEBUG
		if (Success)
			printf("[control] Model reloaded successfully\n");
		else
			printf("[control] ERROR: Failed to reload model\n");
	#endif
}

inline
void
toggle_recording(GlobalState *AppState)
{
	if (!is_whisper_model_loaded(&AppState->WhisperState))
	{
		#ifdef DEBUG
			printf("[control] toggle_recording: no model loaded, ignoring\n");
		#endif
		return;
	}

	if (AppState->IsStreaming)
	{
		#ifdef DEBUG
			printf("[control] toggle_recording: streaming is active, ignoring\n");
		#endif
		return;
	}

	AppState->IsRecording = !AppState->IsRecording;

	if (AppState->IsRecording)
	{
		bool Started = start_record_pipeline(AppState);
		if (!Started)
		{
			AppState->IsRecording = false;
			#ifdef DEBUG
				printf("[control] toggle_recording: failed to start record pipeline\n");
			#endif
			return;
		}
		if (AppState->PlayRecordSound) play_start_recording_sound();
	}
	else
	{
		if (AppState->PlayRecordSound) play_stop_recording_sound();
		signal_record_stop(AppState);
	}

	#ifdef DEBUG
		printf("[control] Recording toggled to: %s\n", AppState->IsRecording ? "true" : "false");
	#endif
}

inline
void
cancel_recording(GlobalState *AppState)
{
	if (!AppState->IsRecording) return;

	#ifdef DEBUG
		printf("[control] cancel_recording: cancelling\n");
	#endif

	AppState->CancelRequested.store(true);
	signal_record_stop(AppState);
	if (AppState->PlayRecordSound) play_cancel_recording_sound();

	AppState->IsRecording = false;
}

inline
void
toggle_streaming(GlobalState *AppState)
{
	if (!is_whisper_model_loaded(&AppState->WhisperState))
	{
		#ifdef DEBUG
			printf("[control] toggle_streaming: no model loaded, ignoring\n");
		#endif
		return;
	}

	if (AppState->IsRecording)
	{
		#ifdef DEBUG
			printf("[control] toggle_streaming: recording is active, ignoring\n");
		#endif
		return;
	}

	AppState->IsStreaming = !AppState->IsStreaming;

	if (AppState->IsStreaming)
	{
		bool Started = start_streaming_pipeline(AppState);
		if (!Started)
		{
			AppState->IsStreaming = false;
			#ifdef DEBUG
				printf("[control] toggle_streaming: failed to start streaming pipeline\n");
			#endif
			return;
		}
	}
	else
	{
		stop_streaming_pipeline(AppState);
	}

	#ifdef DEBUG
		printf("[control] Streaming toggled to: %s\n", AppState->IsStreaming ? "true" : "false");
	#endif
}

inline
void
toggle_stt_model_load(GlobalState *AppState)
{
	#ifdef DEBUG
		printf("[control] toggle_stt_model_load called\n");
	#endif

	if (AppState->IsRecording || AppState->IsStreaming || AppState->CaptureRunning.load())
	{
		#ifdef DEBUG
			printf("[control] toggle_stt_model_load: busy (recording/streaming/transcribing), ignoring\n");
		#endif
		return;
	}

	if (is_whisper_model_loaded(&AppState->WhisperState))
	{
		#ifdef DEBUG
			printf("[control] Unloading STT model...\n");
		#endif
		unload_whisper_model(&AppState->WhisperState);

		#ifdef DEBUG
			printf("[control] STT model unloaded\n");
		#endif
	}
	else
	{
		int ModelIdx = AppState->CurrentSTTModelIndex;
		if (ModelIdx < 0 || ModelIdx >= (int)AppState->STTModelAvailable.size() ||
			!AppState->STTModelAvailable[ModelIdx])
		{
			#ifdef DEBUG
				printf("[control] toggle_stt_model_load: selected model not available on disk\n");
			#endif
			return;
		}

		bool Success = load_whisper_model(
			&AppState->WhisperState, ModelIdx, AppState->CurrentInferenceDeviceIndex);

		#ifdef DEBUG
			if (Success)
				printf("[control] STT model loaded successfully\n");
			else
				printf("[control] ERROR: Failed to load STT model\n");
		#endif
	}
}
