#pragma once

#include "state.h"
#include "audio_pipeline.h"
#include "settings.h"
#include "sounds.h"

inline
void
runtime_update_audio_input_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentAudioDeviceIndex = Index;
}

static
void
runtime_model_transition_thread(GlobalState *AppState, int ModelIndex,
	int InferenceDeviceIndex, bool UnloadCurrentModel,
	ModelTransitionFailure FailureCode)
{
	bool Success = true;

	if (UnloadCurrentModel)
		unload_whisper_model(&AppState->WhisperState);

	if (ModelIndex >= 0)
	{
		Success = load_whisper_model(
			&AppState->WhisperState,
			AppState->STTModelPaths[ModelIndex].c_str(),
			ModelIndex, InferenceDeviceIndex);
	}

	if (!Success)
		AppState->ModelTransitionFailureCode.store((int)FailureCode);

	AppState->IsModelTransitioning.store(false);
}

inline
ModelTransitionFailure
runtime_finish_model_transition(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load())
		return MODEL_TRANSITION_FAILURE_NONE;

	if (!AppState->ModelTransitionThread.joinable())
		return MODEL_TRANSITION_FAILURE_NONE;

	AppState->ModelTransitionThread.join();

	return (ModelTransitionFailure)AppState->ModelTransitionFailureCode.exchange(
		(int)MODEL_TRANSITION_FAILURE_NONE);
}

inline
bool
runtime_start_model_transition(GlobalState *AppState, int ModelIndex,
	int InferenceDeviceIndex, bool UnloadCurrentModel,
	ModelTransitionFailure FailureCode)
{
	runtime_finish_model_transition(AppState);

	if (AppState->IsModelTransitioning.load())
		return false;

	if (AppState->ModelTransitionThread.joinable())
		AppState->ModelTransitionThread.join();

	AppState->ModelTransitionFailureCode.store((int)MODEL_TRANSITION_FAILURE_NONE);
	AppState->IsModelTransitioning.store(true);
	AppState->ModelTransitionThread = std::thread(
		runtime_model_transition_thread,
		AppState,
		ModelIndex,
		InferenceDeviceIndex,
		UnloadCurrentModel,
		FailureCode);

	return true;
}

inline
ModelTransitionFailure
runtime_update_inference_device_selection(GlobalState *AppState, int Index)
{
	if (Index < 0 || Index >= (int)AppState->InferenceDevices.size())
		return MODEL_TRANSITION_FAILURE_NONE;

	int PreviousIndex = AppState->CurrentInferenceDeviceIndex;
	if (PreviousIndex == Index)
		return MODEL_TRANSITION_FAILURE_NONE;

	AppState->CurrentInferenceDeviceIndex = Index;
	save_string_setting("inference_device", AppState->InferenceDevices[Index].c_str());

	if (!is_whisper_model_loaded(&AppState->WhisperState))
		return MODEL_TRANSITION_FAILURE_NONE;

	if (AppState->IsModelTransitioning.load() || AppState->PipelineActive.load())
		return MODEL_TRANSITION_FAILURE_NONE;

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	int ModelIndex = AppState->CurrentSTTModelIndex;
	if (ModelIndex < 0 || ModelIndex >= (int)AppState->STTModelPaths.size())
		ModelIndex = AppState->WhisperState.LoadedModelIndex;

	if (ModelIndex < 0 || ModelIndex >= (int)AppState->STTModelPaths.size())
		return MODEL_TRANSITION_FAILURE_TRANSFER;

	if (!runtime_start_model_transition(AppState, ModelIndex, Index,
		true, MODEL_TRANSITION_FAILURE_TRANSFER))
	{
		return MODEL_TRANSITION_FAILURE_TRANSFER;
	}

	return MODEL_TRANSITION_FAILURE_NONE;
}

inline
void
runtime_update_whisper_thread_count(GlobalState *AppState, int Count)
{
	if (Count < 1) Count = 1;
	AppState->WhisperThreadCount = Count;
}

inline
ModelTransitionFailure
runtime_update_stt_model_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentSTTModelIndex = Index;

	if (!is_whisper_model_loaded(&AppState->WhisperState)) return MODEL_TRANSITION_FAILURE_NONE;
	if (AppState->WhisperState.LoadedModelIndex == Index) return MODEL_TRANSITION_FAILURE_NONE;
	if (AppState->IsModelTransitioning.load()) return MODEL_TRANSITION_FAILURE_NONE;
	if (AppState->PipelineActive.load()) return MODEL_TRANSITION_FAILURE_NONE;

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	if (!runtime_start_model_transition(AppState, Index,
		AppState->CurrentInferenceDeviceIndex,
		false, MODEL_TRANSITION_FAILURE_RELOAD))
	{
		return MODEL_TRANSITION_FAILURE_RELOAD;
	}

	return MODEL_TRANSITION_FAILURE_NONE;
}

inline
bool
runtime_start_recording(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load())
		return false;

	if (!is_whisper_model_loaded(&AppState->WhisperState))
		return false;

	if (AppState->IsStreaming)
		return false;

	if (AppState->IsRecording)
		return true;

	AppState->IsRecording = true;

	bool Started = start_record_pipeline(AppState);
	if (!Started)
	{
		AppState->IsRecording = false;
		return false;
	}

	if (AppState->PlayRecordSound) play_start_recording_sound(&AppState->Platform, AppState->StartSoundFreq);
	return true;
}

inline
void
runtime_stop_recording(GlobalState *AppState)
{
	if (!AppState->IsRecording)
		return;

	AppState->IsRecording = false;
	if (AppState->PlayRecordSound) play_stop_recording_sound(&AppState->Platform, AppState->StopSoundFreq);
	signal_record_stop(AppState);
}

inline
void
runtime_toggle_recording(GlobalState *AppState)
{
	if (AppState->IsRecording)
	{
		runtime_stop_recording(AppState);
	}
	else
	{
		runtime_start_recording(AppState);
	}
}

inline
void
runtime_cancel_recording(GlobalState *AppState)
{
	if (!AppState->IsRecording) return;

	AppState->CancelRequested.store(true);
	signal_record_stop(AppState);
	if (AppState->PlayRecordSound) play_cancel_recording_sound(&AppState->Platform, AppState->CancelSoundFreq);

	AppState->IsRecording = false;
}

inline
void
runtime_toggle_streaming(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load())
		return;

	if (!is_whisper_model_loaded(&AppState->WhisperState))
		return;

	if (AppState->IsRecording)
		return;

	AppState->IsStreaming = !AppState->IsStreaming;

	if (AppState->IsStreaming)
	{
		bool Started = start_streaming_pipeline(AppState);
		if (!Started)
		{
			AppState->IsStreaming = false;
			return;
		}
	}
	else
	{
		stop_streaming_pipeline(AppState);
	}
}

inline
ModelTransitionFailure
runtime_toggle_stt_model_load(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load())
		return MODEL_TRANSITION_FAILURE_NONE;

	if (AppState->IsRecording || AppState->IsStreaming ||
		AppState->CaptureRunning.load() || AppState->PipelineActive.load())
	{
		return MODEL_TRANSITION_FAILURE_NONE;
	}

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	if (is_whisper_model_loaded(&AppState->WhisperState))
	{
		unload_whisper_model(&AppState->WhisperState);
	}
	else
	{
		int ModelIdx = AppState->CurrentSTTModelIndex;
		if (ModelIdx < 0 || ModelIdx >= (int)AppState->STTModelPaths.size())
			return MODEL_TRANSITION_FAILURE_NONE;

		if (!runtime_start_model_transition(AppState, ModelIdx,
			AppState->CurrentInferenceDeviceIndex,
			false, MODEL_TRANSITION_FAILURE_LOAD))
		{
			return MODEL_TRANSITION_FAILURE_LOAD;
		}
	}

	return MODEL_TRANSITION_FAILURE_NONE;
}
