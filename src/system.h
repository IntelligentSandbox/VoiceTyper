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
	{
		AppState->CurrentAudioDeviceIndex = 0;
		for (int i = 0; i < (int)AppState->AudioInputDevices.size(); i++)
		{
			if (AppState->AudioInputDevices[i].IsDefault)
			{
				AppState->CurrentAudioDeviceIndex = i;
				break;
			}
		}
	}
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
}

inline
void
query_hotkey_settings(GlobalState *AppState)
{
	AppState->RecordHotkey       = default_record_hotkey();
	AppState->CancelRecordHotkey = default_cancel_record_hotkey();
	AppState->StreamHotkey       = default_stream_hotkey();
	AppState->LoadModelHotkey    = default_load_model_hotkey();
	AppState->RecordHotkeyMode   = default_recording_hotkey_mode();

	int Modifiers = 0, Key = 0;

	if (load_hotkey_setting("record_hotkey", &Modifiers, &Key))
	{
		AppState->RecordHotkey.Modifiers = (AppHotkeyModifiers)Modifiers;
		AppState->RecordHotkey.VirtualKey = (AppKeyCode)Key;
	}

	if (load_hotkey_setting("cancel_record_hotkey", &Modifiers, &Key))
	{
		AppState->CancelRecordHotkey.Modifiers = (AppHotkeyModifiers)Modifiers;
		AppState->CancelRecordHotkey.VirtualKey = (AppKeyCode)Key;
	}

	if (load_hotkey_setting("stream_hotkey", &Modifiers, &Key))
	{
		AppState->StreamHotkey.Modifiers = (AppHotkeyModifiers)Modifiers;
		AppState->StreamHotkey.VirtualKey = (AppKeyCode)Key;
	}

	if (load_hotkey_setting("load_model_hotkey", &Modifiers, &Key))
	{
		AppState->LoadModelHotkey.Modifiers = (AppHotkeyModifiers)Modifiers;
		AppState->LoadModelHotkey.VirtualKey = (AppKeyCode)Key;
	}

	int RecordHotkeyMode = 0;
	if (load_int_setting("record_hotkey_mode", &RecordHotkeyMode) &&
		is_valid_recording_hotkey_mode(RecordHotkeyMode))
	{
		AppState->RecordHotkeyMode = (RecordingHotkeyMode)RecordHotkeyMode;
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
}
