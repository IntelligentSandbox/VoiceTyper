#include "state.h"

#include <chrono>
#include <thread>
#include <atomic>
#include <cstdio>

inline int
query_logical_processor_count()
{
	unsigned int Count = std::thread::hardware_concurrency();
	return (Count > 0) ? (int)Count : 1;
}

#include "host_services.h"

#include "settings.h"

#include "ggml-backend.h"

// ---------------------------------------------------------------------------
// System queries
// ---------------------------------------------------------------------------
inline void
query_audio_input_devices(GlobalState *AppState)
{
	AppState->CurrentAudioDeviceIndex = -1;

	std::vector<AudioInputDeviceInfo> NativeDevices = platform_query_audio_devices();

	AppState->AudioInputDevices = NativeDevices;
	AppState->AudioInputDeviceNames.clear();
	AppState->AudioInputDeviceNames.reserve(AppState->AudioInputDevices.size());
	for (const AudioInputDeviceInfo &Device : AppState->AudioInputDevices)
	{
		AppState->AudioInputDeviceNames.push_back(Device.Name);
	}

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

inline void
query_inference_devices(GlobalState *AppState)
{
	AppState->InferenceDevices.clear();
	AppState->InferenceDevices.push_back("CPU");
	AppState->CurrentInferenceDeviceIndex = 0;

	std::string SavedDevice;
	if (load_string_setting("inference_device", &SavedDevice))
	{
		if (SavedDevice == "CPU")
		{
			AppState->CurrentInferenceDeviceIndex = 0;
			AppState->InferenceDevicePrefersCpu = true;
		}
		else
		{
			AppState->PendingInferenceDeviceName = SavedDevice;
		}
	}
}

inline void
load_cpu_backend()
{
	std::string ExeDir = platform_get_exe_dir();

#ifdef _WIN32
	std::string PluginPath = platform_join_path(ExeDir, "ggml-cpu.dll");
#else
	std::string PluginPath = platform_join_path(ExeDir, "libggml-cpu.so");
#endif

	FILE *F = std::fopen(PluginPath.c_str(), "rb");
	if (!F) return;
	std::fclose(F);

	ggml_backend_load(PluginPath.c_str());
}

inline void
refresh_inference_devices(GlobalState *AppState)
{
	if (AppState->InferenceDevicesLoaded.load(std::memory_order_acquire)) return;
	if (AppState->InferenceDevicesLoading.exchange(true)) return;

	AppState->InferenceDevicesThread = std::thread([AppState]()
	{
		std::string ExeDir = platform_get_exe_dir();

#ifdef _WIN32
		std::string PluginPath = platform_join_path(ExeDir, "ggml-cuda.dll");
#else
		std::string PluginPath = platform_join_path(ExeDir, "cuda/libggml-cuda.so");
#endif

		FILE *F = std::fopen(PluginPath.c_str(), "rb");
		if (!F)
		{
			AppState->InferenceDevicesLoaded.store(true, std::memory_order_release);
			AppState->InferenceDevicesLoading.store(false);
			return;
		}
		std::fclose(F);

		ggml_backend_reg_t Reg = ggml_backend_load(PluginPath.c_str());
		if (Reg == nullptr)
		{
			AppState->InferenceDevicesLoaded.store(true, std::memory_order_release);
			AppState->InferenceDevicesLoading.store(false);
			return;
		}

		std::vector<std::string> NewDevices;
		NewDevices.push_back("CPU");

		size_t DevCount = ggml_backend_dev_count();
		for (size_t i = 0; i < DevCount; i++)
		{
			ggml_backend_dev_t Dev = ggml_backend_dev_get(i);
			if (!Dev) continue;

			if (ggml_backend_dev_type(Dev) != GGML_BACKEND_DEVICE_TYPE_GPU) continue;

			const char *Desc = ggml_backend_dev_description(Dev);
			std::string Label = "GPU: ";
			Label += (Desc ? Desc : "Unknown");
			NewDevices.push_back(Label);
		}

		int NewIndex = 0;
		if (!AppState->PendingInferenceDeviceName.empty())
		{
			for (int i = 0; i < (int)NewDevices.size(); i++)
			{
				if (NewDevices[i] == AppState->PendingInferenceDeviceName)
				{
					NewIndex = i;
					break;
				}
			}
			AppState->PendingInferenceDeviceName.clear();
		}

		if (NewIndex == 0 && NewDevices.size() > 1 && !AppState->InferenceDevicePrefersCpu)
		{
			NewIndex = 1;
		}

		AppState->InferenceDevices = NewDevices;
		AppState->CurrentInferenceDeviceIndex = NewIndex;
		AppState->InferenceDevicesLoaded.store(true, std::memory_order_release);
		AppState->InferenceDevicesLoading.store(false);
	});
}

inline void
query_whisper_thread_count(GlobalState *AppState)
{
	int LogicalCores = query_logical_processor_count();
	int ThreadCount  = (LogicalCores * 3) / 4;
	if (ThreadCount < 1) ThreadCount = 1;

	AppState->WhisperThreadCount = ThreadCount;
}

inline void
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
	if (load_bool_setting("play_record_sound", &SoundEnabled)) AppState->PlayRecordSound = SoundEnabled;

	int IntVal = 0;
	if (load_int_setting("start_sound_freq", &IntVal)) AppState->StartSoundFreq = IntVal;
	if (load_int_setting("stop_sound_freq", &IntVal)) AppState->StopSoundFreq = IntVal;
	if (load_int_setting("cancel_sound_freq", &IntVal)) AppState->CancelSoundFreq = IntVal;

	bool CharByChar = false;
	if (load_bool_setting("use_char_by_char_injection", &CharByChar)) AppState->UseCharByCharInjection = CharByChar;

	bool CopyToClipboard = false;
	if (load_bool_setting("copy_to_clipboard_when_no_target", &CopyToClipboard))
		AppState->CopyToClipboardWhenNoTarget = CopyToClipboard;

	bool ShowConfidence = false;
	if (load_bool_setting("show_transcribed_text_confidence", &ShowConfidence))
		AppState->ShowTranscribedTextConfidence = ShowConfidence;
}
