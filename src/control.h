#pragma once

#include "runtime_control.h"

#include "imgui.h"

#define TOAST_DURATION_SECONDS 2.0
#define TOAST_COLOR_ERROR    ColorRgba{0.70f, 0.10f, 0.10f, 1.0f}
#define TOAST_COLOR_SUCCESS  ColorRgba{0.10f, 0.55f, 0.20f, 1.0f}

inline
void
show_toast_with_color(GlobalState *AppState, const char *Message, const ColorRgba &BackgroundColor)
{
	AppState->Ui.ToastMessage = Message;
	AppState->Ui.ToastExpireTime = ImGui::GetTime() + TOAST_DURATION_SECONDS;
	AppState->Ui.ToastBackgroundColor = BackgroundColor;
	AppState->Ui.ToastSerial += 1;
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
show_model_transition_failure(GlobalState *AppState, ModelTransitionFailure FailureCode)
{
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
void
update_audio_input_selection(GlobalState *AppState, int Index)
{
	runtime_update_audio_input_selection(AppState, Index);
}

inline
void
finish_model_transition(GlobalState *AppState)
{
	show_model_transition_failure(AppState, runtime_finish_model_transition(AppState));
}

inline
bool
start_model_transition(GlobalState *AppState, int ModelIndex,
	int InferenceDeviceIndex, bool UnloadCurrentModel,
	ModelTransitionFailure FailureCode)
{
	return runtime_start_model_transition(AppState, ModelIndex,
		InferenceDeviceIndex, UnloadCurrentModel, FailureCode);
}

inline
void
update_inference_device_selection(GlobalState *AppState, int Index)
{
	show_model_transition_failure(AppState, runtime_update_inference_device_selection(AppState, Index));
}

inline
void
update_whisper_thread_count(GlobalState *AppState, int Count)
{
	runtime_update_whisper_thread_count(AppState, Count);
}

inline
void
update_stt_model_selection(GlobalState *AppState, int Index)
{
	show_model_transition_failure(AppState, runtime_update_stt_model_selection(AppState, Index));
}

inline
bool
start_recording(GlobalState *AppState)
{
	return runtime_start_recording(AppState);
}

inline
void
stop_recording(GlobalState *AppState)
{
	runtime_stop_recording(AppState);
}

inline
void
toggle_recording(GlobalState *AppState)
{
	runtime_toggle_recording(AppState);
}

inline
void
cancel_recording(GlobalState *AppState)
{
	runtime_cancel_recording(AppState);
}

inline
void
toggle_streaming(GlobalState *AppState)
{
	runtime_toggle_streaming(AppState);
}

inline
void
toggle_stt_model_load(GlobalState *AppState)
{
	show_model_transition_failure(AppState, runtime_toggle_stt_model_load(AppState));
}
