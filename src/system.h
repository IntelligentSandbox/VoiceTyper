#include "state.h"
#include "host_services.h"
#include "settings.h"
#include "ggml-backend.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdio>

inline char
ascii_lower(char Ch)
{
	return (Ch >= 'A' && Ch <= 'Z') ? (char)(Ch - 'A' + 'a') : Ch;
}

inline bool
ascii_less_ci(const std::string &A, const std::string &B)
{
	size_t Count = A.size() < B.size() ? A.size() : B.size();
	for (size_t i = 0; i < Count; i++)
	{
		char Ca = ascii_lower(A[i]);
		char Cb = ascii_lower(B[i]);
		if (Ca != Cb)
		{
			return Ca < Cb;
		}
	}
	return A.size() < B.size();
}

inline bool
ascii_equals_ci(const std::string &A, const std::string &B)
{
	if (A.size() != B.size()) return false;
	for (size_t i = 0; i < A.size(); i++)
	{
		if (ascii_lower(A[i]) != ascii_lower(B[i]))
		{
			return false;
		}
	}
	return true;
}

inline bool
ascii_contains_ci(const std::string &Text, const std::string &LowerNeedle)
{
	if (LowerNeedle.empty()) return true;

	size_t Matched = 0;
	for (size_t i = 0; i < Text.size(); i++)
	{
		char Ch = ascii_lower(Text[i]);
		if (Ch == LowerNeedle[Matched])
		{
			Matched++;
			if (Matched == LowerNeedle.size())
			{
				return true;
			}
		}
		else
		{
			Matched = (Ch == LowerNeedle[0]) ? 1 : 0;
		}
	}
	return false;
}

inline int
query_logical_processor_count()
{
	unsigned int Count = std::thread::hardware_concurrency();
	return (Count > 0) ? (int)Count : 1;
}

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

		std::string SavedDevice;
		if (load_string_setting("audio_input_device", &SavedDevice) && !SavedDevice.empty())
		{
			for (int i = 0; i < (int)AppState->AudioInputDevices.size(); i++)
			{
				if (AppState->AudioInputDevices[i].Name == SavedDevice)
				{
					AppState->CurrentAudioDeviceIndex = i;
					break;
				}
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
	std::string ExeDir = platform_get_binary_dir();

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
		std::string ExeDir = platform_get_binary_dir();

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
query_font_names(GlobalState *AppState)
{
	std::vector<PlatformFontInfo> Fonts = platform_enumerate_fonts();

	std::vector<std::string> Names;
	Names.reserve(Fonts.size());
	for (const PlatformFontInfo &Font : Fonts)
	{
		if (!Font.Name.empty())
		{
			Names.push_back(Font.Name);
		}
	}

	std::sort(Names.begin(), Names.end(), ascii_less_ci);

	AppState->UiFontNames.clear();
	for (const std::string &Name : Names)
	{
		if (!AppState->UiFontNames.empty() &&
			ascii_equals_ci(AppState->UiFontNames.back(), Name))
		{
			continue;
		}
		AppState->UiFontNames.push_back(Name);
	}
}

inline void query_paste_hotkey_overrides(GlobalState *AppState);

inline void
query_hotkey_settings(GlobalState *AppState)
{
	AppState->RecordHotkey       = default_record_hotkey();
	AppState->CancelRecordHotkey = default_cancel_record_hotkey();
	AppState->StreamHotkey       = default_stream_hotkey();
	AppState->LoadModelHotkey    = default_load_model_hotkey();
	AppState->PasteHotkey        = default_paste_hotkey();
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

	if (load_hotkey_setting("paste_hotkey", &Modifiers, &Key))
	{
		AppState->PasteHotkey.Modifiers = (AppHotkeyModifiers)Modifiers;
		AppState->PasteHotkey.VirtualKey = (AppKeyCode)Key;
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

	bool PreserveClipboard = false;
	if (load_bool_setting("preserve_clipboard_on_paste", &PreserveClipboard))
		AppState->PreserveClipboardOnPaste = PreserveClipboard;

	int ClipboardRestoreDelayMs = 0;
	if (load_int_setting("clipboard_restore_delay_ms", &ClipboardRestoreDelayMs))
	{
		if (ClipboardRestoreDelayMs >= 0 && ClipboardRestoreDelayMs <= 10000)
			AppState->ClipboardRestoreDelayMs = ClipboardRestoreDelayMs;
	}

	std::string UiFontName;
	if (load_string_setting("ui_font_name", &UiFontName)) AppState->UiFontName = UiFontName;

	std::string WhisperInitialPrompt;
	if (load_string_setting("whisper_initial_prompt", &WhisperInitialPrompt))
		AppState->WhisperInitialPrompt = WhisperInitialPrompt;

	int UiFontSize = 0;
	if (load_int_setting("ui_font_size", &UiFontSize))
	{
		if (UiFontSize >= 6 && UiFontSize <= 200) AppState->UiFontSize = UiFontSize;
	}

	query_paste_hotkey_overrides(AppState);
}

inline void
query_paste_hotkey_overrides(GlobalState *AppState)
{
	AppState->PasteHotkeyOverrides.clear();

	const std::string Prefix = PASTE_HOTKEY_OVERRIDE_SETTING_PREFIX;
	const std::string ModSuffix = "_modifiers";

	auto Map = read_settings_map();
	for (const auto &Pair : Map)
	{
		if (Pair.first.compare(0, Prefix.size(), Prefix) != 0) continue;
		if (Pair.first.size() <= Prefix.size() + ModSuffix.size()) continue;
		if (Pair.first.compare(Pair.first.size() - ModSuffix.size(), ModSuffix.size(), ModSuffix) != 0) continue;

		std::string ProcessName = Pair.first.substr(Prefix.size(),
			Pair.first.size() - ModSuffix.size() - Prefix.size());
		if (ProcessName.empty()) continue;

		auto KeyIt = Map.find(Prefix + ProcessName + "_key");
		if (KeyIt == Map.end()) continue;

		int Modifiers = 0;
		int Key = 0;
		try
		{
			Modifiers = std::stoi(Pair.second);
			Key = std::stoi(KeyIt->second);
		}
		catch (...)
		{
			continue;
		}

		PasteHotkeyOverride Override;
		Override.ProcessName = ProcessName;
		Override.Hotkey.Modifiers = (AppHotkeyModifiers)Modifiers;
		Override.Hotkey.VirtualKey = (AppKeyCode)Key;
		if (!Override.Hotkey.is_valid()) continue;

		AppState->PasteHotkeyOverrides.push_back(Override);
	}
}

inline void
upsert_paste_hotkey_override(GlobalState *AppState, const std::string &ProcessName, HotkeyConfig Hotkey)
{
	bool Found = false;
	{
		std::lock_guard<std::mutex> Lock(AppState->PasteHotkeyOverridesMutex);
		for (PasteHotkeyOverride &Override : AppState->PasteHotkeyOverrides)
		{
			if (ascii_equals_ci(Override.ProcessName, ProcessName))
			{
				Override.Hotkey = Hotkey;
				Found = true;
				break;
			}
		}

		if (!Found)
		{
			PasteHotkeyOverride Override;
			Override.ProcessName = ProcessName;
			Override.Hotkey = Hotkey;
			AppState->PasteHotkeyOverrides.push_back(Override);
		}
	}

	save_paste_hotkey_override_setting(ProcessName, Hotkey);
}

inline void
remove_paste_hotkey_override(GlobalState *AppState, const std::string &ProcessName)
{
	{
		std::lock_guard<std::mutex> Lock(AppState->PasteHotkeyOverridesMutex);
		for (size_t i = 0; i < AppState->PasteHotkeyOverrides.size(); )
		{
			if (ascii_equals_ci(AppState->PasteHotkeyOverrides[i].ProcessName, ProcessName))
				AppState->PasteHotkeyOverrides.erase(AppState->PasteHotkeyOverrides.begin() + i);
			else i++;
		}
	}

	remove_paste_hotkey_override_setting(ProcessName);
}

inline bool
resolve_font_path(const std::string &FontName, std::string *OutPath)
{
	if (FontName.empty()) return false;

	std::vector<PlatformFontInfo> Fonts = platform_enumerate_fonts();

	std::string Lower;
	for (char Ch : FontName)
	{
		Lower += (Ch >= 'A' && Ch <= 'Z') ? (char)(Ch - 'A' + 'a') : Ch;
	}

	for (const PlatformFontInfo &Font : Fonts)
	{
		if (Font.Name == FontName)
		{
			*OutPath = Font.Path;
			return true;
		}
	}

	for (const PlatformFontInfo &Font : Fonts)
	{
		std::string Candidate;
		for (char Ch : Font.Name)
		{
			Candidate += (Ch >= 'A' && Ch <= 'Z') ? (char)(Ch - 'A' + 'a') : Ch;
		}
		if (Candidate == Lower)
		{
			*OutPath = Font.Path;
			return true;
		}
	}

	return false;
}
