#pragma once

#include "state.h"
#include "audio_pipeline.h"
#include "settings.h"
#include "sounds.h"

#define TOAST_DURATION_SECONDS 2.0

inline
void
show_toast_with_color(GlobalState *AppState, const char *Message, const ImVec4 &BackgroundColor)
{
	AppState->ToastMessage = Message;
	AppState->ToastExpireTime = ImGui::GetTime() + TOAST_DURATION_SECONDS;
	AppState->ToastBackgroundColor = BackgroundColor;
	AppState->ToastSerial += 1;
}

inline
void
show_toast(GlobalState *AppState, const char *Message)
{
	show_toast_with_color(AppState, Message, TOAST_COLOR_ERROR);
}

inline
void
show_success_toast(GlobalState *AppState, const char *Message)
{
	show_toast_with_color(AppState, Message, TOAST_COLOR_SUCCESS);
}

inline
void
update_audio_input_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentAudioDeviceIndex = Index;
}

static
void
model_transition_thread(GlobalState *AppState, int ModelIndex,
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
void
finish_model_transition(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load())
		return;

	if (!AppState->ModelTransitionThread.joinable())
		return;

	AppState->ModelTransitionThread.join();

	ModelTransitionFailure FailureCode =
		(ModelTransitionFailure)AppState->ModelTransitionFailureCode.exchange(
			(int)MODEL_TRANSITION_FAILURE_NONE);

	switch (FailureCode)
	{
	case MODEL_TRANSITION_FAILURE_LOAD:
		show_toast(AppState, "Failed to load STT model");
		break;
	case MODEL_TRANSITION_FAILURE_RELOAD:
		show_toast(AppState, "Failed to reload STT model");
		break;
	case MODEL_TRANSITION_FAILURE_TRANSFER:
		show_toast(AppState, "Failed to reload model on new inference device");
		break;
	case MODEL_TRANSITION_FAILURE_NONE:
	default:
		break;
	}
}

inline
bool
start_model_transition(GlobalState *AppState, int ModelIndex,
	int InferenceDeviceIndex, bool UnloadCurrentModel,
	ModelTransitionFailure FailureCode)
{
	finish_model_transition(AppState);

	if (AppState->IsModelTransitioning.load())
		return false;

	if (AppState->ModelTransitionThread.joinable())
		AppState->ModelTransitionThread.join();

	AppState->ModelTransitionFailureCode.store((int)MODEL_TRANSITION_FAILURE_NONE);
	AppState->IsModelTransitioning.store(true);
	AppState->ModelTransitionThread = std::thread(
		model_transition_thread,
		AppState,
		ModelIndex,
		InferenceDeviceIndex,
		UnloadCurrentModel,
		FailureCode);

	return true;
}

inline
void
update_inference_device_selection(GlobalState *AppState, int Index)
{
	if (Index < 0 || Index >= (int)AppState->InferenceDevices.size())
		return;

	int PreviousIndex = AppState->CurrentInferenceDeviceIndex;
	if (PreviousIndex == Index)
		return;

	AppState->CurrentInferenceDeviceIndex = Index;
	save_string_setting("inference_device", AppState->InferenceDevices[Index].c_str());

	if (!is_whisper_model_loaded(&AppState->WhisperState))
		return;

	if (AppState->IsModelTransitioning.load() || AppState->PipelineActive.load())
		return;

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	int ModelIndex = AppState->CurrentSTTModelIndex;
	if (ModelIndex < 0 || ModelIndex >= (int)AppState->STTModelPaths.size())
		ModelIndex = AppState->WhisperState.LoadedModelIndex;

	if (ModelIndex < 0 || ModelIndex >= (int)AppState->STTModelPaths.size())
	{
		show_toast(AppState, "Failed to reload model on new inference device");
		return;
	}

	if (!start_model_transition(AppState, ModelIndex, Index,
		true, MODEL_TRANSITION_FAILURE_TRANSFER))
	{
		show_toast(AppState, "Failed to reload model on new inference device");
	}
}

inline
void
update_whisper_thread_count(GlobalState *AppState, int Count)
{
	if (Count < 1) Count = 1;
	AppState->WhisperThreadCount = Count;
}

inline
void
update_stt_model_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentSTTModelIndex = Index;

	if (!is_whisper_model_loaded(&AppState->WhisperState)) return;
	if (AppState->WhisperState.LoadedModelIndex == Index) return;
	if (AppState->IsModelTransitioning.load()) return;
	if (AppState->PipelineActive.load()) return;

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	if (!start_model_transition(AppState, Index,
		AppState->CurrentInferenceDeviceIndex,
		false, MODEL_TRANSITION_FAILURE_RELOAD))
	{
		show_toast(AppState, "Failed to reload STT model");
	}
}

inline
void
toggle_recording(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load())
		return;

	if (!is_whisper_model_loaded(&AppState->WhisperState))
		return;

	if (AppState->IsStreaming)
		return;

	AppState->IsRecording = !AppState->IsRecording;

	if (AppState->IsRecording)
	{
		bool Started = start_record_pipeline(AppState);
		if (!Started)
		{
			AppState->IsRecording = false;
			return;
		}
		if (AppState->PlayRecordSound) play_start_recording_sound(&AppState->StartSound);
	}
	else
	{
		if (AppState->PlayRecordSound) play_stop_recording_sound(&AppState->StopSound);
		signal_record_stop(AppState);
	}
}

inline
void
cancel_recording(GlobalState *AppState)
{
	if (!AppState->IsRecording) return;

	AppState->CancelRequested.store(true);
	signal_record_stop(AppState);
	if (AppState->PlayRecordSound) play_cancel_recording_sound(&AppState->CancelSound);

	AppState->IsRecording = false;
}

inline
void
toggle_streaming(GlobalState *AppState)
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
void
toggle_stt_model_load(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load())
	{
		return;
	}

	if (AppState->IsRecording || AppState->IsStreaming ||
		AppState->CaptureRunning.load() || AppState->PipelineActive.load())
	{
		return;
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
			return;

		if (!start_model_transition(AppState, ModelIdx,
			AppState->CurrentInferenceDeviceIndex,
			false, MODEL_TRANSITION_FAILURE_LOAD))
		{
			show_toast(AppState, "Failed to load STT model");
		}
	}
}
