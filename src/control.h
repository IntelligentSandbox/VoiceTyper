#pragma once

#include "state.h"
#include "audio_pipeline.h"
#include "settings.h"
#include "sounds.h"

#define TOAST_DURATION_SECONDS 4.0

inline
void
show_toast(GlobalState *AppState, const char *Message)
{
	AppState->ToastMessage = Message;
	AppState->ToastExpireTime = ImGui::GetTime() + TOAST_DURATION_SECONDS;
}

inline
void
update_audio_input_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentAudioDeviceIndex = Index;
}

inline
void
update_inference_device_selection(GlobalState *AppState, int Index)
{
	int PreviousIndex = AppState->CurrentInferenceDeviceIndex;
	AppState->CurrentInferenceDeviceIndex = Index;

	if (Index >= 0 && Index < (int)AppState->InferenceDevices.size())
		save_string_setting("inference_device", AppState->InferenceDevices[Index].c_str());

	if (PreviousIndex == Index) return;
	if (!is_whisper_model_loaded(&AppState->WhisperState)) return;
	if (AppState->PipelineActive.load()) return;

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	bool Success = load_whisper_model(
		&AppState->WhisperState,
		AppState->WhisperState.ModelPath.c_str(),
		AppState->WhisperState.LoadedModelIndex, Index);
	if (!Success)
		show_toast(AppState, "Failed to reload model on new inference device");
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
	if (AppState->PipelineActive.load()) return;

	if (AppState->CaptureThread.joinable())
		AppState->CaptureThread.join();

	bool Success = load_whisper_model(
		&AppState->WhisperState,
		AppState->STTModelPaths[Index].c_str(),
		Index, AppState->CurrentInferenceDeviceIndex);
	if (!Success)
		show_toast(AppState, "Failed to reload STT model");
}

inline
void
toggle_recording(GlobalState *AppState)
{
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

		bool Success = load_whisper_model(
			&AppState->WhisperState,
			AppState->STTModelPaths[ModelIdx].c_str(),
			ModelIdx, AppState->CurrentInferenceDeviceIndex);
		if (!Success)
			show_toast(AppState, "Failed to load STT model");
	}
}
