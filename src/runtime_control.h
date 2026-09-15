#pragma once

#include "state.h"
#include "audio_pipeline.h"
#include "input.h"
#include "settings.h"
#include "sounds.h"

#include <chrono>

inline void runtime_start_pending_pipelines(GlobalState *AppState);

inline void
runtime_update_audio_input_selection(GlobalState *AppState, int Index)
{
	if (Index < 0 || Index >= (int)AppState->AudioInputDevices.size()) return;

	AppState->CurrentAudioDeviceIndex = Index;
	save_string_setting("audio_input_device", AppState->AudioInputDevices[Index].Name.c_str());
}

static void
runtime_model_transition_thread(GlobalState *AppState, int ModelIndex,
	int InferenceDeviceIndex, bool UnloadCurrentModel,
	ModelTransitionFailure FailureCode)
{
	bool Success = true;

	if (UnloadCurrentModel) unload_whisper_model(&AppState->WhisperState);

	if (ModelIndex >= 0)
	{
		std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
		Success = load_whisper_model(
			&AppState->WhisperState,
			AppState->STTModelPaths[ModelIndex].c_str(),
			ModelIndex, InferenceDeviceIndex);
		std::chrono::steady_clock::time_point End = std::chrono::steady_clock::now();
		double Ms = std::chrono::duration<double, std::milli>(End - Start).count();
		if (Success) AppState->LastModelLoadMs.store(Ms);
	}

	if (!Success) AppState->ModelTransitionFailureCode.store((int)FailureCode);

	AppState->IsModelTransitioning.store(false);
}

inline ModelTransitionFailure
runtime_finish_model_transition(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load()) return MODEL_TRANSITION_FAILURE_NONE;

	if (!AppState->ModelTransitionThread.joinable()) return MODEL_TRANSITION_FAILURE_NONE;

	AppState->ModelTransitionThread.join();

	ModelTransitionFailure Failure = (ModelTransitionFailure)AppState->ModelTransitionFailureCode.exchange(
		(int)MODEL_TRANSITION_FAILURE_NONE);

	if (Failure == MODEL_TRANSITION_FAILURE_NONE)
	{
		runtime_start_pending_pipelines(AppState);
	}

	return Failure;
}

inline bool
runtime_start_model_transition(GlobalState *AppState, int ModelIndex,
	int InferenceDeviceIndex, bool UnloadCurrentModel,
	ModelTransitionFailure FailureCode)
{
	runtime_finish_model_transition(AppState);

	if (AppState->IsModelTransitioning.load()) return false;

	if (AppState->ModelTransitionThread.joinable()) AppState->ModelTransitionThread.join();

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

inline ModelTransitionFailure
runtime_update_inference_device_selection(GlobalState *AppState, int Index)
{
	// Resolution path for the inference-device selection vs. the background
	// GPU probe (refresh_inference_devices in system.h):
	//   * The worker builds the GPU list in a local vector and only publishes
	//     it to InferenceDevices (and flips InferenceDevicesLoaded) once the
	//     probe finishes. So while !Loaded the list contains only "CPU" and a
	//     GPU index is out of range — the bounds check below rejects it.
	//   * The UI dropdown (imgui_ui.h) only renders the real list once Loaded
	//     is true, so a user can't pick a GPU entry before the probe completes.
	//   * A SAVED GPU preference that is still pending is stashed into
	//     PendingInferenceDeviceName by query_inference_devices at startup and
	//     resolved by the worker once the GPU list is published.
	// Should a GPU index (Index > 0) ever reach us before the list is loaded
	// (programmatic caller / future UI), defer by saving the desired name and
	// letting the worker resolve it — but the current architecture makes this
	// unreachable, so we simply treat an out-of-range selection as a no-op.
	if (Index < 0 || Index >= (int)AppState->InferenceDevices.size()) return MODEL_TRANSITION_FAILURE_NONE;

	int PreviousIndex = AppState->CurrentInferenceDeviceIndex;
	if (PreviousIndex == Index) return MODEL_TRANSITION_FAILURE_NONE;

	AppState->CurrentInferenceDeviceIndex = Index;
	save_string_setting("inference_device", AppState->InferenceDevices[Index].c_str());

	if (!is_whisper_model_loaded(&AppState->WhisperState)) return MODEL_TRANSITION_FAILURE_NONE;

	if (AppState->IsModelTransitioning.load() || AppState->PipelineActive.load() ||
		AppState->PendingRecordOnModelLoad || AppState->PendingStreamOnModelLoad)
	{
		return MODEL_TRANSITION_FAILURE_NONE;
	}

	if (AppState->CaptureThread.joinable()) AppState->CaptureThread.join();

	int ModelIndex = AppState->CurrentSTTModelIndex;
	if (ModelIndex < 0 || ModelIndex >= (int)AppState->STTModelPaths.size()) ModelIndex = AppState->WhisperState.LoadedModelIndex;

	if (ModelIndex < 0 || ModelIndex >= (int)AppState->STTModelPaths.size()) return MODEL_TRANSITION_FAILURE_TRANSFER;

	if (!runtime_start_model_transition(AppState, ModelIndex, Index,
		true, MODEL_TRANSITION_FAILURE_TRANSFER))
	{
		return MODEL_TRANSITION_FAILURE_TRANSFER;
	}

	return MODEL_TRANSITION_FAILURE_NONE;
}

inline void
runtime_update_whisper_thread_count(GlobalState *AppState, int Count)
{
	if (Count < 1) Count = 1;
	AppState->WhisperThreadCount = Count;
}

inline ModelTransitionFailure
runtime_update_stt_model_selection(GlobalState *AppState, int Index)
{
	if (Index < 0 || Index >= (int)AppState->STTModelPaths.size()) return MODEL_TRANSITION_FAILURE_NONE;

	AppState->CurrentSTTModelIndex = Index;

	const std::string &ModelPath = AppState->STTModelPaths[Index];
	size_t Slash = ModelPath.find_last_of("\\/");
	std::string FileName = (Slash == std::string::npos) ? ModelPath : ModelPath.substr(Slash + 1);
	save_string_setting("stt_model", FileName.c_str());

	if (!is_whisper_model_loaded(&AppState->WhisperState)) return MODEL_TRANSITION_FAILURE_NONE;
	if (AppState->WhisperState.LoadedModelIndex == Index) return MODEL_TRANSITION_FAILURE_NONE;
	if (AppState->IsModelTransitioning.load()) return MODEL_TRANSITION_FAILURE_NONE;
	if (AppState->PendingRecordOnModelLoad || AppState->PendingStreamOnModelLoad) return MODEL_TRANSITION_FAILURE_NONE;
	if (AppState->PipelineActive.load()) return MODEL_TRANSITION_FAILURE_NONE;

	if (AppState->CaptureThread.joinable()) AppState->CaptureThread.join();

	if (!runtime_start_model_transition(AppState, Index,
		AppState->CurrentInferenceDeviceIndex,
		false, MODEL_TRANSITION_FAILURE_RELOAD))
	{
		return MODEL_TRANSITION_FAILURE_RELOAD;
	}

	return MODEL_TRANSITION_FAILURE_NONE;
}

inline bool
runtime_model_matches_selection(GlobalState *AppState)
{
	if (!is_whisper_model_loaded(&AppState->WhisperState)) return false;
	if (AppState->WhisperState.LoadedModelIndex != AppState->CurrentSTTModelIndex) return false;
	if (AppState->WhisperState.LoadedInferenceDeviceIndex != AppState->CurrentInferenceDeviceIndex) return false;
	return true;
}

// The user hit the Record/Stream hotkey but the selected model is not loaded
// on the selected inference device. Load it on the model-transition thread and
// start the requested pipeline once the load succeeds (see
// runtime_start_pending_pipelines). Returns false: the pipeline has not
// started yet in any case.
inline bool
runtime_request_model_load_for_hotkey(GlobalState *AppState, bool ForRecording)
{
	int ModelIndex = AppState->CurrentSTTModelIndex;
	if (ModelIndex < 0 || ModelIndex >= (int)AppState->STTModelPaths.size()) return false;

	if (AppState->PipelineActive.load()) return false;

	if (AppState->CaptureThread.joinable()) AppState->CaptureThread.join();

	AppState->PendingRecordOnModelLoad = ForRecording;
	AppState->PendingStreamOnModelLoad = !ForRecording;

	if (!runtime_start_model_transition(AppState, ModelIndex,
		AppState->CurrentInferenceDeviceIndex,
		true, MODEL_TRANSITION_FAILURE_LOAD))
	{
		AppState->PendingRecordOnModelLoad = false;
		AppState->PendingStreamOnModelLoad = false;
	}

	return false;
}

inline bool
runtime_start_recording(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load())
	{
		AppState->PendingRecordOnModelLoad = true;
		AppState->PendingStreamOnModelLoad = false;
		return false;
	}

	if (AppState->IsStreaming) return false;

	if (!runtime_model_matches_selection(AppState))
	{
		return runtime_request_model_load_for_hotkey(AppState, true);
	}

	if (AppState->IsRecording) return true;

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

inline void
runtime_stop_recording(GlobalState *AppState)
{
	if (!AppState->IsRecording) return;

	AppState->IsRecording = false;
	if (AppState->PlayRecordSound) play_stop_recording_sound(&AppState->Platform, AppState->StopSoundFreq);
	signal_record_stop(AppState);
}

inline void
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

inline void
runtime_cancel_recording(GlobalState *AppState)
{
	AppState->PendingRecordOnModelLoad = false;

	if (!AppState->IsRecording) return;

	AppState->CancelRequested.store(true);
	signal_record_stop(AppState);
	if (AppState->PlayRecordSound) play_cancel_recording_sound(&AppState->Platform, AppState->CancelSoundFreq);

	AppState->IsRecording = false;
}

inline void
runtime_toggle_streaming(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load())
	{
		AppState->PendingStreamOnModelLoad = true;
		AppState->PendingRecordOnModelLoad = false;
		return;
	}

	if (AppState->IsRecording) return;

	if (AppState->IsStreaming)
	{
		AppState->IsStreaming = false;
		stop_streaming_pipeline(AppState);
		return;
	}

	if (!runtime_model_matches_selection(AppState))
	{
		runtime_request_model_load_for_hotkey(AppState, false);
		return;
	}

	AppState->IsStreaming = true;

	bool Started = start_streaming_pipeline(AppState);
	if (!Started)
	{
		AppState->IsStreaming = false;
		return;
	}
}

inline void
runtime_start_pending_pipelines(GlobalState *AppState)
{
	bool WantsRecord = AppState->PendingRecordOnModelLoad;
	bool WantsStream = AppState->PendingStreamOnModelLoad;
	AppState->PendingRecordOnModelLoad = false;
	AppState->PendingStreamOnModelLoad = false;

	if (!WantsRecord && !WantsStream) return;

	if (!is_whisper_model_loaded(&AppState->WhisperState)) return;

	if (WantsRecord)
	{
		// Hold mode: if the user already released the key while the model was
		// loading there is nothing to record — drop the request instead of
		// starting a recording nothing will stop.
		if (AppState->RecordHotkeyMode != RECORDING_HOTKEY_TOGGLE &&
			!is_hotkey_down(AppState->RecordHotkey))
		{
			return;
		}

		runtime_start_recording(AppState);
		return;
	}

	runtime_toggle_streaming(AppState);
}

inline ModelTransitionFailure
runtime_toggle_stt_model_load(GlobalState *AppState)
{
	if (AppState->IsModelTransitioning.load()) return MODEL_TRANSITION_FAILURE_NONE;

	if (AppState->IsRecording || AppState->IsStreaming ||
		AppState->CaptureRunning.load() || AppState->PipelineActive.load() ||
		AppState->PendingRecordOnModelLoad || AppState->PendingStreamOnModelLoad)
	{
		return MODEL_TRANSITION_FAILURE_NONE;
	}

	if (AppState->CaptureThread.joinable()) AppState->CaptureThread.join();

	if (is_whisper_model_loaded(&AppState->WhisperState))
	{
		// ModelIndex -1 = unload only. whisper_free of a GPU-backed context
		// touches CUDA (buffer frees can synchronize the device), so it must
		// run on the model-transition worker, never on the UI thread.
		runtime_start_model_transition(AppState, -1, AppState->CurrentInferenceDeviceIndex,
			true, MODEL_TRANSITION_FAILURE_NONE);
	}
	else
	{
		int ModelIdx = AppState->CurrentSTTModelIndex;
		if (ModelIdx < 0 || ModelIdx >= (int)AppState->STTModelPaths.size()) return MODEL_TRANSITION_FAILURE_NONE;

		if (!runtime_start_model_transition(AppState, ModelIdx,
			AppState->CurrentInferenceDeviceIndex,
			false, MODEL_TRANSITION_FAILURE_LOAD))
		{
			return MODEL_TRANSITION_FAILURE_LOAD;
		}
	}

	return MODEL_TRANSITION_FAILURE_NONE;
}
