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
// Hotkey detection
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// File existence check (used by model query)
// ---------------------------------------------------------------------------
inline
bool
file_exists(const char *Path)
{
	DWORD Attrib = GetFileAttributesA(Path);
	return (Attrib != INVALID_FILE_ATTRIBUTES && !(Attrib & FILE_ATTRIBUTE_DIRECTORY));
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
		AppState->RecordHotkey.Modifiers = (UINT)Modifiers;
		AppState->RecordHotkey.VirtualKey = (UINT)Key;
	}

	if (load_hotkey_setting("cancel_record_hotkey", &Modifiers, &Key))
	{
		AppState->CancelRecordHotkey.Modifiers = (UINT)Modifiers;
		AppState->CancelRecordHotkey.VirtualKey = (UINT)Key;
	}

	if (load_hotkey_setting("stream_hotkey", &Modifiers, &Key))
	{
		AppState->StreamHotkey.Modifiers = (UINT)Modifiers;
		AppState->StreamHotkey.VirtualKey = (UINT)Key;
	}

	if (load_hotkey_setting("load_model_hotkey", &Modifiers, &Key))
	{
		AppState->LoadModelHotkey.Modifiers = (UINT)Modifiers;
		AppState->LoadModelHotkey.VirtualKey = (UINT)Key;
	}

	bool SoundEnabled = false;
	if (load_bool_setting("play_record_sound", &SoundEnabled))
		AppState->PlayRecordSound = SoundEnabled;

	int IntVal = 0;
	if (load_int_setting("start_sound_freq", &IntVal))
		AppState->StartSound.FreqHz = IntVal;
	if (load_int_setting("start_sound_volume", &IntVal))
		AppState->StartSound.Volume = IntVal;
	if (load_int_setting("stop_sound_freq", &IntVal))
		AppState->StopSound.FreqHz = IntVal;
	if (load_int_setting("stop_sound_volume", &IntVal))
		AppState->StopSound.Volume = IntVal;
	if (load_int_setting("cancel_sound_freq", &IntVal))
		AppState->CancelSound.FreqHz = IntVal;
	if (load_int_setting("cancel_sound_volume", &IntVal))
		AppState->CancelSound.Volume = IntVal;

	bool CharByChar = false;
	if (load_bool_setting("use_char_by_char_injection", &CharByChar))
		AppState->UseCharByCharInjection = CharByChar;

	#ifdef DEBUG
		printf("[system] Record hotkey:     %s\n", AppState->RecordHotkey.to_label().c_str());
		printf("[system] Stream hotkey:     %s\n", AppState->StreamHotkey.to_label().c_str());
		printf("[system] Load model hotkey: %s\n", AppState->LoadModelHotkey.to_label().c_str());
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
