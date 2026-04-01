#include "state.h"

#ifndef VOICETYPER_USE_IMGUI
	#include "control.h"
#endif

#include <chrono>
#include <thread>
#include <atomic>

inline
int
query_logical_processor_count()
{
	unsigned int Count = std::thread::hardware_concurrency();
	return (Count > 0) ? (int)Count : 1;
}

#include "platform.h"

#include "settings.h"

#ifdef _WIN32
	#include <windows.h>
	#include "platform_win32.h"
#endif

#ifdef VOICETYPER_CUDA
	#include "ggml-cuda.h"
#endif

// ---------------------------------------------------------------------------
// Application icon
// ---------------------------------------------------------------------------
#ifndef VOICETYPER_USE_IMGUI
inline
void
set_application_icon(GlobalState *AppState)
{
	QPixmap Pixmap(APP_ICON_PATH);
	if (Pixmap.isNull())
	{
		#ifdef DEBUG
			printf("[system] WARNING: Could not load icon from %s\n", APP_ICON_PATH);
		#endif
		return;
	}
	QIcon Icon(Pixmap);
	AppState->QtMainWindow->setWindowIcon(Icon);
	AppState->QtApp->setWindowIcon(Icon);
	#ifdef DEBUG
		printf("[system] Application icon set from %s\n", APP_ICON_PATH);
	#endif
}
#endif

// ---------------------------------------------------------------------------
// Hotkey detection
// ---------------------------------------------------------------------------
#ifdef VOICETYPER_USE_IMGUI

inline
bool
check_modifier_state(UINT Modifiers)
{
	bool CtrlRequired  = (Modifiers & HOTKEY_MOD_CTRL)  != 0;
	bool AltRequired   = (Modifiers & HOTKEY_MOD_ALT)   != 0;
	bool ShiftRequired = (Modifiers & HOTKEY_MOD_SHIFT) != 0;
	bool WinRequired   = (Modifiers & HOTKEY_MOD_WIN)   != 0;

	bool CtrlDown  = platform_is_key_down(VK_CONTROL);
	bool AltDown   = platform_is_key_down(VK_MENU);
	bool ShiftDown = platform_is_key_down(VK_SHIFT);
	bool WinDown   = platform_is_key_down(VK_LWIN);

	if (CtrlRequired  && !CtrlDown)  return false;
	if (AltRequired   && !AltDown)   return false;
	if (ShiftRequired && !ShiftDown) return false;
	if (WinRequired   && !WinDown)   return false;

	if (!CtrlRequired  && CtrlDown)  return false;
	if (!AltRequired   && AltDown)   return false;
	if (!ShiftRequired && ShiftDown) return false;

	return true;
}

inline
bool
is_hotkey_down(const HotkeyConfig &Config)
{
	if (!check_modifier_state(Config.Modifiers)) return false;
	if (Config.VirtualKey == 0) return true;
	return platform_is_key_down(Config.VirtualKey);
}

#else // Qt build

// Maps a Qt::KeyboardModifiers bitmask to platform key-down checks.
// Returns true only when ALL requested modifiers are held.
inline
bool
check_qt_modifiers(Qt::KeyboardModifiers Modifiers)
{
	bool CtrlRequired  = (Modifiers & Qt::ControlModifier) != 0;
	bool AltRequired   = (Modifiers & Qt::AltModifier)     != 0;
	bool ShiftRequired = (Modifiers & Qt::ShiftModifier)   != 0;
	bool MetaRequired  = (Modifiers & Qt::MetaModifier)    != 0;

	bool CtrlDown  = platform_is_key_down(platform_qt_key_to_native(Qt::Key_Control));
	bool AltDown   = platform_is_key_down(platform_qt_key_to_native(Qt::Key_Alt));
	bool ShiftDown = platform_is_key_down(platform_qt_key_to_native(Qt::Key_Shift));
	bool MetaDown  = platform_is_key_down(platform_qt_key_to_native(Qt::Key_Meta));

	// Every required modifier must be held; no extra modifiers allowed
	// (prevents misfires when the user is pressing unrelated combos)
	if (CtrlRequired  && !CtrlDown)  return false;
	if (AltRequired   && !AltDown)   return false;
	if (ShiftRequired && !ShiftDown) return false;
	if (MetaRequired  && !MetaDown)  return false;

	if (!CtrlRequired  && CtrlDown)  return false;
	if (!AltRequired   && AltDown)   return false;
	if (!ShiftRequired && ShiftDown) return false;

	return true;
}

// Returns true when the given HotkeyConfig is currently pressed.
inline
bool
is_hotkey_down(const HotkeyConfig &Config)
{
	if (!check_qt_modifiers(Config.Modifiers)) return false;

	int NativeKey = platform_qt_key_to_native(Config.Key);
	if (NativeKey == 0)
	{
		// Modifier-only hotkey: just check modifiers (already done above)
		return true;
	}
	return platform_is_key_down(NativeKey);
}

std::atomic<bool> HotkeyThreadRunning = false;

inline
void
hotkey_thread_func(GlobalState *AppState)
{
	bool RecordKeyWasDown       = false;
	bool CancelRecordKeyWasDown = false;
	bool StreamKeyWasDown       = false;
	bool LoadModelKeyWasDown    = false;

	while (HotkeyThreadRunning)
	{
		// Skip all hotkey checks if the settings dialog is open (remapping mode)
		if (AppState->IsSettingsDialogOpen)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			continue;
		}

		bool RecordKeyIsDown       = is_hotkey_down(AppState->RecordHotkey);
		bool CancelRecordKeyIsDown = is_hotkey_down(AppState->CancelRecordHotkey);
		bool StreamKeyIsDown       = is_hotkey_down(AppState->StreamHotkey);
		bool LoadModelKeyIsDown    = is_hotkey_down(AppState->LoadModelHotkey);

		if (RecordKeyIsDown && !RecordKeyWasDown)
		{
			QMetaObject::invokeMethod(
				AppState->QtApp,
				[AppState]() { toggle_recording(AppState); },
				Qt::QueuedConnection);
		}

		if (CancelRecordKeyIsDown && !CancelRecordKeyWasDown)
		{
			QMetaObject::invokeMethod(
				AppState->QtApp,
				[AppState]() { cancel_recording(AppState); },
				Qt::QueuedConnection);
		}

		if (StreamKeyIsDown && !StreamKeyWasDown)
		{
			QMetaObject::invokeMethod(
				AppState->QtApp,
				[AppState]() { toggle_streaming(AppState); },
				Qt::QueuedConnection);
		}

		if (LoadModelKeyIsDown && !LoadModelKeyWasDown)
		{
			QMetaObject::invokeMethod(
				AppState->QtApp,
				[AppState]() { toggle_stt_model_load(AppState); },
				Qt::QueuedConnection);
		}

		RecordKeyWasDown       = RecordKeyIsDown;
		CancelRecordKeyWasDown = CancelRecordKeyIsDown;
		StreamKeyWasDown       = StreamKeyIsDown;
		LoadModelKeyWasDown    = LoadModelKeyIsDown;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

inline
void
start_hotkey_listener(GlobalState *AppState)
{
	HotkeyThreadRunning = true;
	std::thread(hotkey_thread_func, AppState).detach();
}

inline
void
stop_hotkey_listener()
{
	HotkeyThreadRunning = false;
}

#endif // VOICETYPER_USE_IMGUI

// ---------------------------------------------------------------------------
// File existence check (used by model query)
// ---------------------------------------------------------------------------
inline
bool
file_exists(const char *Path)
{
#ifdef VOICETYPER_USE_IMGUI
	DWORD Attrib = GetFileAttributesA(Path);
	return (Attrib != INVALID_FILE_ATTRIBUTES && !(Attrib & FILE_ATTRIBUTE_DIRECTORY));
#else
	return QFile::exists(Path);
#endif
}

// ---------------------------------------------------------------------------
// System queries
// ---------------------------------------------------------------------------
inline
void
query_audio_input_devices(GlobalState *AppState)
{
	AppState->CurrentAudioDeviceIndex = -1;

	std::vector<AudioInputDeviceInfo> NativeDevices = platform_query_audio_devices();

	AppState->AudioInputDevices = NativeDevices;

	if (AppState->AudioInputDevices.size() > 0)
		AppState->CurrentAudioDeviceIndex = 0;

	#ifdef DEBUG
		printf("[system] Audio input devices found: %d\n", (int)AppState->AudioInputDevices.size());
		for (const auto &Info : AppState->AudioInputDevices)
		{
			printf("[system]   Device: %s (index %d)\n", Info.Name.c_str(), Info.Index);
		}
	#endif
}

inline
void
query_inference_devices(GlobalState *AppState)
{
	AppState->InferenceDevices.clear();
	AppState->InferenceDevices.push_back("CPU");
	AppState->CurrentInferenceDeviceIndex = 0;

#ifdef VOICETYPER_CUDA
	int DeviceCount = ggml_backend_cuda_get_device_count();
	for (int i = 0; i < DeviceCount; i++)
	{
		char Description[128] = {};
		ggml_backend_cuda_get_device_description(i, Description, sizeof(Description));

		std::string Label = "GPU: ";
		Label += Description;
		AppState->InferenceDevices.push_back(Label);

		#ifdef DEBUG
			size_t FreeMem = 0, TotalMem = 0;
			ggml_backend_cuda_get_device_memory(i, &FreeMem, &TotalMem);
			printf("[system] CUDA device %d: %s (%.0f MB free / %.0f MB total)\n",
				i, Description,
				(double)FreeMem / (1024.0 * 1024.0),
				(double)TotalMem / (1024.0 * 1024.0));
		#endif
	}
#endif

	std::string SavedDevice;
	if (load_string_setting("inference_device", &SavedDevice))
	{
		for (int i = 0; i < (int)AppState->InferenceDevices.size(); i++)
		{
			if (AppState->InferenceDevices[i] == SavedDevice)
			{
				AppState->CurrentInferenceDeviceIndex = i;
				#ifdef DEBUG
					printf("[system] Restored inference device: %s (index %d)\n",
						SavedDevice.c_str(), i);
				#endif
				break;
			}
		}
	}
}

inline
void
query_whisper_thread_count(GlobalState *AppState)
{
	int LogicalCores = query_logical_processor_count();
	int ThreadCount  = LogicalCores / 2;
	if (ThreadCount < 1) ThreadCount = 1;

	AppState->WhisperThreadCount = ThreadCount;

	#ifdef DEBUG
		printf("[system] Logical processors: %d, whisper thread count: %d\n",
			LogicalCores, ThreadCount);
	#endif
}

inline
void
query_hotkey_settings(GlobalState *AppState)
{
	AppState->RecordHotkey       = default_record_hotkey();
	AppState->CancelRecordHotkey = default_cancel_record_hotkey();
	AppState->StreamHotkey       = default_stream_hotkey();
	AppState->LoadModelHotkey    = default_load_model_hotkey();

	int Modifiers = 0, Key = 0;

	if (load_hotkey_setting("record_hotkey", &Modifiers, &Key))
	{
#ifdef VOICETYPER_USE_IMGUI
		AppState->RecordHotkey.Modifiers = (UINT)Modifiers;
		AppState->RecordHotkey.VirtualKey = (UINT)Key;
#else
		AppState->RecordHotkey.Modifiers = Qt::KeyboardModifiers(Modifiers);
		AppState->RecordHotkey.Key       = Qt::Key(Key);
#endif
	}

	if (load_hotkey_setting("cancel_record_hotkey", &Modifiers, &Key))
	{
#ifdef VOICETYPER_USE_IMGUI
		AppState->CancelRecordHotkey.Modifiers = (UINT)Modifiers;
		AppState->CancelRecordHotkey.VirtualKey = (UINT)Key;
#else
		AppState->CancelRecordHotkey.Modifiers = Qt::KeyboardModifiers(Modifiers);
		AppState->CancelRecordHotkey.Key       = Qt::Key(Key);
#endif
	}

	if (load_hotkey_setting("stream_hotkey", &Modifiers, &Key))
	{
#ifdef VOICETYPER_USE_IMGUI
		AppState->StreamHotkey.Modifiers = (UINT)Modifiers;
		AppState->StreamHotkey.VirtualKey = (UINT)Key;
#else
		AppState->StreamHotkey.Modifiers = Qt::KeyboardModifiers(Modifiers);
		AppState->StreamHotkey.Key       = Qt::Key(Key);
#endif
	}

	if (load_hotkey_setting("load_model_hotkey", &Modifiers, &Key))
	{
#ifdef VOICETYPER_USE_IMGUI
		AppState->LoadModelHotkey.Modifiers = (UINT)Modifiers;
		AppState->LoadModelHotkey.VirtualKey = (UINT)Key;
#else
		AppState->LoadModelHotkey.Modifiers = Qt::KeyboardModifiers(Modifiers);
		AppState->LoadModelHotkey.Key       = Qt::Key(Key);
#endif
	}

	bool SoundEnabled = false;
	if (load_bool_setting("play_record_sound", &SoundEnabled))
		AppState->PlayRecordSound = SoundEnabled;

	bool CharByChar = false;
	if (load_bool_setting("use_char_by_char_injection", &CharByChar))
		AppState->UseCharByCharInjection = CharByChar;

	#ifdef DEBUG
	#ifdef VOICETYPER_USE_IMGUI
		printf("[system] Record hotkey:     %s\n", AppState->RecordHotkey.to_label().c_str());
		printf("[system] Stream hotkey:     %s\n", AppState->StreamHotkey.to_label().c_str());
		printf("[system] Load model hotkey: %s\n", AppState->LoadModelHotkey.to_label().c_str());
	#else
		printf("[system] Record hotkey:     %s\n", AppState->RecordHotkey.to_label().toUtf8().constData());
		printf("[system] Stream hotkey:     %s\n", AppState->StreamHotkey.to_label().toUtf8().constData());
		printf("[system] Load model hotkey: %s\n", AppState->LoadModelHotkey.to_label().toUtf8().constData());
	#endif
		printf("[system] Play record sound: %s\n", AppState->PlayRecordSound ? "true" : "false");
	#endif
}

inline
void
query_available_stt_models(GlobalState *AppState)
{
	AppState->STTModels.clear();
	AppState->STTModelAvailable.clear();

	AppState->STTModels.push_back("Whisper tiny.en (75 MB)");
	AppState->STTModels.push_back("Whisper base.en (142 MB)");
	// AppState->STTModels.push_back("Whisper small.en (466 MB)");
	// AppState->STTModels.push_back("Whisper medium.en (1.5 GB)");
	// AppState->STTModels.push_back("Whisper large-v3-turbo (1.5 GB)");

	AppState->AnySTTModelAvailable = false;
	int FirstAvailableIndex = -1;
	for (int i = 0; i < WHISPER_MODEL_COUNT; i++)
	{
		bool Exists = file_exists(WHISPER_MODEL_PATHS[i]);
		AppState->STTModelAvailable.push_back(Exists);
		if (Exists && FirstAvailableIndex == -1) FirstAvailableIndex = i;
		if (Exists) AppState->AnySTTModelAvailable = true;
		#ifdef DEBUG
			printf("[system] Model '%s' (%s): %s\n",
				AppState->STTModels[i].c_str(),
				WHISPER_MODEL_PATHS[i],
				Exists ? "found" : "NOT FOUND");
		#endif
	}

	if (FirstAvailableIndex != -1)
	{
		AppState->CurrentSTTModelIndex =
			(FirstAvailableIndex <= WHISPER_MODEL_BASE_EN &&
			 AppState->STTModelAvailable[WHISPER_MODEL_BASE_EN])
			? WHISPER_MODEL_BASE_EN : FirstAvailableIndex;
	}
	else
	{
		AppState->CurrentSTTModelIndex = 0;
	}
}
