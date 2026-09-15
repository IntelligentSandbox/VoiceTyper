#pragma once

#include "input.h"
#include "model_assets.h"
#include "model_downloader.h"
#include "runtime_control.h"
#include "system.h"
#include "updater.h"

#include <cstring>

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
	AppState->PendingRecordOnModelLoad = false;
	AppState->PendingStreamOnModelLoad = false;
	AppState->CaptureRunning = false;
	AppState->PipelineActive = false;
	AppState->StreamingFinalizeOnStop = false;
	AppState->IsModelTransitioning.store(false);
	AppState->ModelTransitionFailureCode.store((int)MODEL_TRANSITION_FAILURE_NONE);
	AppState->ExitRequested.store(false);
	AppState->Platform.OwnWindow = OwnWindow;
	AppState->Ui.SettingsState.SelectedAction = 0;
	AppState->Ui.SettingsState.LastPreviewTime = -1.0;
	AppState->Ui.SettingsState.FontNameBuffer[0] = '\0';
	AppState->Ui.SettingsState.FontNameBufferInitialized = false;
	AppState->Ui.SettingsState.FontSuggestionIndex = -1;
	AppState->Ui.SettingsState.FontSuggestionMatchCount = 0;
	AppState->Ui.SettingsState.WhisperPromptBuffer[0] = '\0';
	AppState->Ui.SettingsState.WhisperPromptBufferInitialized = false;
	AppState->Ui.SettingsState.Capture.Captured = AppState->RecordHotkey;
	AppState->Ui.SettingsState.Capture.HasCapture = AppState->RecordHotkey.is_valid();
	AppState->Ui.SettingsState.Capture.IsCapturing = false;
	AppState->Ui.SettingsState.Capture.PeakModifiers = 0;
	AppState->Ui.SettingsState.Capture.PeakVirtualKey = 0;
	AppState->Ui.SettingsState.Capture.ReleaseFrames = 0;
	AppState->Ui.SettingsState.NewPasteOverrideProcess[0] = '\0';
	AppState->Ui.SettingsState.PasteOverrideCapture.Captured = {};
	AppState->Ui.SettingsState.PasteOverrideCapture.HasCapture = false;
	AppState->Ui.SettingsState.PasteOverrideCapture.IsCapturing = false;
	AppState->Ui.SettingsState.PasteOverrideCapture.PeakModifiers = 0;
	AppState->Ui.SettingsState.PasteOverrideCapture.PeakVirtualKey = 0;
	AppState->Ui.SettingsState.PasteOverrideCapture.ReleaseFrames = 0;
	AppState->Ui.SettingsState.PasteOverrideCaptureProcess.clear();
	AppState->Ui.SettingsState.HotkeysModalOpen = false;
	AppState->Ui.IsCrashDialogOpen = false;
	AppState->Ui.CrashDialogOpened = false;
	AppState->Ui.PendingCrashDumps.clear();
	AppState->InferenceDevicePrefersCpu = false;
	AppState->PlayRecordSound = false;
	AppState->StartSoundFreq = SOUND_DEFAULT_START_FREQ;
	AppState->StopSoundFreq = SOUND_DEFAULT_STOP_FREQ;
	AppState->CancelSoundFreq = SOUND_DEFAULT_CANCEL_FREQ;
	AppState->UseCharByCharInjection = false;
	AppState->CopyToClipboardWhenNoTarget = false;
	AppState->PreserveClipboardOnPaste = false;
	AppState->ClipboardRestoreDelayMs = 200;
	AppState->UiFontSize = 18;
	AppState->Ui.FontReloadRequested = false;
	AppState->Ui.LightMode = false;
	load_bool_setting("ui_light_mode", &AppState->Ui.LightMode);
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
	cleanup_partial_model_downloads();
	query_available_stt_models(AppState);
	query_whisper_thread_count(AppState);
	query_hotkey_settings(AppState);
	query_font_names(AppState);

	if (!AppState->Ui.SettingsState.FontNameBufferInitialized)
	{
		strncpy(AppState->Ui.SettingsState.FontNameBuffer, AppState->UiFontName.c_str(),
			sizeof(AppState->Ui.SettingsState.FontNameBuffer) - 1);
		AppState->Ui.SettingsState.FontNameBuffer[sizeof(AppState->Ui.SettingsState.FontNameBuffer) - 1] = '\0';
		AppState->Ui.SettingsState.FontNameBufferInitialized = true;
	}

	if (!AppState->Ui.SettingsState.WhisperPromptBufferInitialized)
	{
		strncpy(AppState->Ui.SettingsState.WhisperPromptBuffer, AppState->WhisperInitialPrompt.c_str(),
			sizeof(AppState->Ui.SettingsState.WhisperPromptBuffer) - 1);
		AppState->Ui.SettingsState.WhisperPromptBuffer[sizeof(AppState->Ui.SettingsState.WhisperPromptBuffer) - 1] = '\0';
		AppState->Ui.SettingsState.WhisperPromptBufferInitialized = true;
	}

	// GGML_BACKEND_DL builds ship the CPU backend as a separate ggml-cpu.dll
	// plugin next to the exe. The ggml backend registry starts empty, so the
	// CPU backend must be explicitly loaded before whisper can initialize it
	// (whisper.cpp's whisper_backend_init throws if no CPU device is
	// registered, breaking every model load — CPU or GPU, since whisper
	// always pulls in a CPU backend). This is fast: just LoadLibrary + CPU
	// feature detection. The slow CUDA probe (refresh_inference_devices,
	// which loads ggml-cuda.dll and runs driver init) is deliberately NOT
	// started here: its DLL loading holds the process loader lock and would
	// stall this thread's own LoadLibrary work. The platform main kicks it
	// only after all UI-thread initialization (fonts, backends) is done.
	// load_cpu_backend() also deliberately loads only ggml-cpu.dll (not
	// ggml_backend_load_all, which would eagerly load ggml-cuda.dll here).
	load_cpu_backend();
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
	shutdown_updater(AppState);

	AppState->StreamingFinalizeOnStop.store(false);
	AppState->CaptureRunning.store(false);
	if (AppState->CaptureThread.joinable()) AppState->CaptureThread.join();
	if (AppState->ModelTransitionThread.joinable()) AppState->ModelTransitionThread.join();
	if (AppState->InferenceDevicesThread.joinable()) AppState->InferenceDevicesThread.join();

	if (is_whisper_model_loaded(&AppState->WhisperState)) unload_whisper_model(&AppState->WhisperState);
}
