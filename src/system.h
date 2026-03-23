#include "state.h"
#include "control.h"

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

#ifdef _WIN32
	#include <windows.h>
	#include "platform_win32.h"
#endif

#ifdef VOICETYPER_CUDA
	#include "ggml-cuda.h"
#endif

std::atomic<bool> HotkeyThreadRunning = false;

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

// TODO(warren): A lot of these functions mix WIN32 dependency in them...oh AI.
// For code quality control, we'll need to split them out eventually...push
// specifics into win32 layer and os agnostic code in here again.
// Might need to better clearly specify the spec of the expected data/function interface.

// Maps a Qt::KeyboardModifiers bitmask to Win32 GetAsyncKeyState checks.
// Returns true only when ALL requested modifiers are held.
inline
bool
check_qt_modifiers_win32(Qt::KeyboardModifiers Modifiers)
{
	bool CtrlRequired  = (Modifiers & Qt::ControlModifier) != 0;
	bool AltRequired   = (Modifiers & Qt::AltModifier)     != 0;
	bool ShiftRequired = (Modifiers & Qt::ShiftModifier)   != 0;
	bool MetaRequired  = (Modifiers & Qt::MetaModifier)    != 0;

	bool CtrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	bool AltDown   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
	bool ShiftDown = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
	bool MetaDown  = (GetAsyncKeyState(VK_LWIN)    & 0x8000) != 0
	              || (GetAsyncKeyState(VK_RWIN)    & 0x8000) != 0;

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

// Maps a Qt::Key to a Win32 virtual key code.
// Returns 0 for Qt::Key_unknown / 0 (modifier-only hotkeys).
inline
int
qt_key_to_vk(Qt::Key Key)
{
	if (Key == Qt::Key_unknown || Key == 0) return 0;

	// Function keys
	if (Key >= Qt::Key_F1 && Key <= Qt::Key_F35)
		return VK_F1 + (Key - Qt::Key_F1);

	// Letters A-Z
	if (Key >= Qt::Key_A && Key <= Qt::Key_Z)
		return 'A' + (Key - Qt::Key_A);

	// Digits 0-9
	if (Key >= Qt::Key_0 && Key <= Qt::Key_9)
		return '0' + (Key - Qt::Key_0);

	switch (Key)
	{
		case Qt::Key_Space:     return VK_SPACE;
		case Qt::Key_Return:    return VK_RETURN;
		case Qt::Key_Escape:    return VK_ESCAPE;
		case Qt::Key_Tab:       return VK_TAB;
		case Qt::Key_Backspace: return VK_BACK;
		case Qt::Key_Delete:    return VK_DELETE;
		case Qt::Key_Insert:    return VK_INSERT;
		case Qt::Key_Home:      return VK_HOME;
		case Qt::Key_End:       return VK_END;
		case Qt::Key_PageUp:    return VK_PRIOR;
		case Qt::Key_PageDown:  return VK_NEXT;
		case Qt::Key_Left:      return VK_LEFT;
		case Qt::Key_Right:     return VK_RIGHT;
		case Qt::Key_Up:        return VK_UP;
		case Qt::Key_Down:      return VK_DOWN;
		default:                return 0;
	}
}

// Returns true when the given HotkeyConfig is currently pressed.
inline
bool
is_hotkey_down(const HotkeyConfig &Config)
{
	if (!check_qt_modifiers_win32(Config.Modifiers)) return false;

	int VK = qt_key_to_vk(Config.Key);
	if (VK == 0)
	{
		// Modifier-only hotkey: just check modifiers (already done above)
		return true;
	}
	return (GetAsyncKeyState(VK) & 0x8000) != 0;
}

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
		#ifdef _WIN32
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
		#endif
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

inline
void
query_audio_input_devices(GlobalState *AppState)
{
	AppState->CurrentAudioDeviceIndex = -1;
	
	std::vector<AudioInputDeviceInfo> NativeDevices = query_audio_input_devices_native();
	
	AppState->AudioInputDevices = NativeDevices;
	
	if (AppState->AudioInputDevices.size() > 0)
	{
		AppState->CurrentAudioDeviceIndex = 0;
	}
	
	#ifdef DEBUG
		qDebug() << "Audio input devices found:" << AppState->AudioInputDevices.size();
		for (const auto &Info : AppState->AudioInputDevices)
		{
			qDebug() << "  Device:" << Info.Name.c_str() << "(index" << Info.Index << ")";
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
		AppState->RecordHotkey.Modifiers = Qt::KeyboardModifiers(Modifiers);
		AppState->RecordHotkey.Key       = Qt::Key(Key);
	}

	if (load_hotkey_setting("cancel_record_hotkey", &Modifiers, &Key))
	{
		AppState->CancelRecordHotkey.Modifiers = Qt::KeyboardModifiers(Modifiers);
		AppState->CancelRecordHotkey.Key       = Qt::Key(Key);
	}

	if (load_hotkey_setting("stream_hotkey", &Modifiers, &Key))
	{
		AppState->StreamHotkey.Modifiers = Qt::KeyboardModifiers(Modifiers);
		AppState->StreamHotkey.Key       = Qt::Key(Key);
	}

	if (load_hotkey_setting("load_model_hotkey", &Modifiers, &Key))
	{
		AppState->LoadModelHotkey.Modifiers = Qt::KeyboardModifiers(Modifiers);
		AppState->LoadModelHotkey.Key       = Qt::Key(Key);
	}

	bool SoundEnabled = false;
	if (load_bool_setting("play_record_sound", &SoundEnabled))
	{
		AppState->PlayRecordSound = SoundEnabled;
	}

	bool CharByChar = false;
	if (load_bool_setting("use_char_by_char_injection", &CharByChar))
	{
		AppState->UseCharByCharInjection = CharByChar;
	}

	#ifdef DEBUG
		printf("[system] Record hotkey:     %s\n", AppState->RecordHotkey.to_label().toUtf8().constData());
		printf("[system] Stream hotkey:     %s\n", AppState->StreamHotkey.to_label().toUtf8().constData());
		printf("[system] Load model hotkey: %s\n", AppState->LoadModelHotkey.to_label().toUtf8().constData());
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
		bool Exists = QFile::exists(WHISPER_MODEL_PATHS[i]);
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
