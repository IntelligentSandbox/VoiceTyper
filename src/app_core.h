#pragma once

#include "input.h"
#include "model_assets.h"
#include "model_downloader.h"
#include "runtime_control.h"
#include "system.h"

struct AppFrameState
{
	bool RecordKeyWasDown;
	bool CancelRecordKeyWasDown;
	bool StreamKeyWasDown;
	bool LoadModelKeyWasDown;
};

struct AppFrameResult
{
	ModelTransitionFailure ModelFailure;
};

inline void
app_initialize_runtime(GlobalState *AppState, PlatformWindowHandle OwnWindow)
{
	AppState->IsRecording = false;
	AppState->IsStreaming = false;
	AppState->CaptureRunning = false;
	AppState->PipelineActive = false;
	AppState->StreamingFinalizeOnStop = false;
	AppState->IsModelTransitioning.store(false);
	AppState->ModelTransitionFailureCode.store((int)MODEL_TRANSITION_FAILURE_NONE);
	AppState->Platform.OwnWindow = OwnWindow;
	AppState->Ui.SettingsState.SelectedAction = 0;
	AppState->Ui.SettingsState.LastPreviewTime = -1.0;
	AppState->Ui.SettingsState.Capture.Captured = AppState->RecordHotkey;
	AppState->Ui.SettingsState.Capture.HasCapture = AppState->RecordHotkey.is_valid();
	AppState->Ui.SettingsState.Capture.IsCapturing = false;
	AppState->Ui.SettingsState.Capture.PeakModifiers = 0;
	AppState->Ui.SettingsState.Capture.PeakVirtualKey = 0;
	AppState->Ui.SettingsState.Capture.ReleaseFrames = 0;
	AppState->Ui.IsCrashDialogOpen = false;
	AppState->Ui.CrashDialogOpened = false;
	AppState->Ui.PendingCrashDumps.clear();
	AppState->PlayRecordSound = false;
	AppState->StartSoundFreq = SOUND_DEFAULT_START_FREQ;
	AppState->StopSoundFreq = SOUND_DEFAULT_STOP_FREQ;
	AppState->CancelSoundFreq = SOUND_DEFAULT_CANCEL_FREQ;
	AppState->UseCharByCharInjection = false;
	AppState->CopyToClipboardWhenNoTarget = false;
	AppState->RecordHotkeyMode = default_recording_hotkey_mode();

	AppState->LastModelLoadMs.store(-1.0);
	AppState->LastTranscriptionMs.store(-1.0);
	AppState->LastPasteMs.store(-1.0);

	init_whisper_state(&AppState->WhisperState);

	cleanup_legacy_settings_json();
	migrate_legacy_data_dir_settings();
	query_vad_model_path(AppState);
	query_audio_input_devices(AppState);
	query_inference_devices(AppState);
	query_available_stt_models(AppState);
	query_whisper_thread_count(AppState);
	query_hotkey_settings(AppState);

	// GGML_BACKEND_DL builds ship the CPU backend as a separate ggml-cpu.dll
	// plugin next to the exe. The ggml backend registry starts empty, so the
	// CPU backend must be explicitly loaded before whisper can initialize it
	// (whisper.cpp's whisper_backend_init throws if no CPU device is
	// registered, breaking every model load — CPU or GPU, since whisper
	// always pulls in a CPU backend). This is fast: just LoadLibrary + CPU
	// feature detection. The slow CUDA driver init stays on the background
	// thread in refresh_inference_devices, which loads ggml-cuda.dll from the
	// exe dir; load_cpu_backend() deliberately loads only ggml-cpu.dll (not
	// ggml_backend_load_all, which would eagerly load ggml-cuda.dll here).
	load_cpu_backend();

	refresh_inference_devices(AppState);
}

inline AppFrameResult
app_update_runtime_frame(GlobalState *AppState, AppFrameState *FrameState, bool HotkeysEnabled)
{
	AppFrameResult Result = {};
	Result.ModelFailure = runtime_finish_model_transition(AppState);

	if (!HotkeysEnabled || AppState->IsModelTransitioning.load()) return Result;

	bool RecordKeyIsDown       = is_hotkey_down(AppState->RecordHotkey);
	bool CancelRecordKeyIsDown = is_hotkey_down(AppState->CancelRecordHotkey);
	bool StreamKeyIsDown       = is_hotkey_down(AppState->StreamHotkey);
	bool LoadModelKeyIsDown    = is_hotkey_down(AppState->LoadModelHotkey);

	if (AppState->RecordHotkeyMode == RECORDING_HOTKEY_TOGGLE)
	{
		if (RecordKeyIsDown && !FrameState->RecordKeyWasDown) runtime_toggle_recording(AppState);
	}
	else
	{
		if (RecordKeyIsDown && !FrameState->RecordKeyWasDown) runtime_start_recording(AppState);
		if (!RecordKeyIsDown && FrameState->RecordKeyWasDown && AppState->IsRecording) runtime_stop_recording(AppState);
	}

	if (CancelRecordKeyIsDown && !FrameState->CancelRecordKeyWasDown) runtime_cancel_recording(AppState);

	if (StreamKeyIsDown && !FrameState->StreamKeyWasDown) runtime_toggle_streaming(AppState);

	if (LoadModelKeyIsDown && !FrameState->LoadModelKeyWasDown) Result.ModelFailure = runtime_toggle_stt_model_load(AppState);

	FrameState->RecordKeyWasDown       = RecordKeyIsDown;
	FrameState->CancelRecordKeyWasDown = CancelRecordKeyIsDown;
	FrameState->StreamKeyWasDown       = StreamKeyIsDown;
	FrameState->LoadModelKeyWasDown    = LoadModelKeyIsDown;

	return Result;
}

inline void
app_shutdown_runtime(GlobalState *AppState)
{
	shutdown_model_download(AppState);

	AppState->StreamingFinalizeOnStop.store(false);
	AppState->CaptureRunning.store(false);
	if (AppState->CaptureThread.joinable()) AppState->CaptureThread.join();
	if (AppState->ModelTransitionThread.joinable()) AppState->ModelTransitionThread.join();
	if (AppState->InferenceDevicesThread.joinable()) AppState->InferenceDevicesThread.join();

	if (is_whisper_model_loaded(&AppState->WhisperState)) unload_whisper_model(&AppState->WhisperState);
}
