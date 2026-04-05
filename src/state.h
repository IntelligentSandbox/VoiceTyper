#pragma once

#include "whisper_wrapper.h"

#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MAX_AUDIO_DEVICE_NAME_LENGTH 512

struct AudioInputDeviceInfo
{
	int Index;
	std::string Id;
	std::string Name;
	bool IsDefault;
};

#define AUDIO_CAPTURE_SAMPLE_RATE       16000
#define AUDIO_CAPTURE_CHANNELS          1
#define AUDIO_CAPTURE_BITS_PER_SAMPLE   16
#define AUDIO_CAPTURE_BUFFER_MS         100
#define AUDIO_CAPTURE_BUFFER_COUNT      8

#define WINDOW_DEFAULT_WIDTH 700
#define WINDOW_DEFAULT_HEIGHT 500

#define APP_ICON_PATH "media/voicetyper-icon.png"

// ---------------------------------------------------------------------------
// Hotkey Config
// ---------------------------------------------------------------------------
#define HOTKEY_MOD_CTRL  0x01
#define HOTKEY_MOD_ALT   0x02
#define HOTKEY_MOD_SHIFT 0x04
#define HOTKEY_MOD_WIN   0x08

struct HotkeyConfig
{
	UINT Modifiers;
	UINT VirtualKey;

	std::string
	to_label() const
	{
		std::string Label;

		if (Modifiers & HOTKEY_MOD_CTRL)  Label += "Ctrl+";
		if (Modifiers & HOTKEY_MOD_ALT)   Label += "Alt+";
		if (Modifiers & HOTKEY_MOD_SHIFT) Label += "Shift+";
		if (Modifiers & HOTKEY_MOD_WIN)   Label += "Win+";

		if (VirtualKey == 0)
		{
			if (!Label.empty() && Label.back() == '+')
				Label.pop_back();
			return Label;
		}

		if (VirtualKey >= VK_F1 && VirtualKey <= VK_F24)
		{
			Label += "F" + std::to_string(VirtualKey - VK_F1 + 1);
		}
		else if (VirtualKey >= 'A' && VirtualKey <= 'Z')
		{
			Label += (char)VirtualKey;
		}
		else if (VirtualKey >= '0' && VirtualKey <= '9')
		{
			Label += (char)VirtualKey;
		}
		else
		{
			switch (VirtualKey)
			{
			case VK_SPACE:   Label += "Space";     break;
			case VK_RETURN:  Label += "Enter";     break;
			case VK_ESCAPE:  Label += "Escape";    break;
			case VK_TAB:     Label += "Tab";       break;
			case VK_BACK:    Label += "Backspace"; break;
			case VK_DELETE:  Label += "Delete";    break;
			case VK_INSERT:  Label += "Insert";    break;
			case VK_HOME:    Label += "Home";      break;
			case VK_END:     Label += "End";       break;
			case VK_PRIOR:   Label += "PageUp";    break;
			case VK_NEXT:    Label += "PageDown";  break;
			case VK_LEFT:    Label += "Left";      break;
			case VK_RIGHT:   Label += "Right";     break;
			case VK_UP:      Label += "Up";        break;
			case VK_DOWN:    Label += "Down";      break;
			default:         Label += "??";        break;
			}
		}

		return Label;
	}

	bool
	is_valid() const
	{
		return Modifiers != 0 || VirtualKey != 0;
	}
};

inline HotkeyConfig default_record_hotkey()
{
	HotkeyConfig H = {};
	H.Modifiers = HOTKEY_MOD_CTRL | HOTKEY_MOD_ALT;
	H.VirtualKey = 0;
	return H;
}

inline HotkeyConfig default_cancel_record_hotkey()
{
	HotkeyConfig H = {};
	H.Modifiers = HOTKEY_MOD_ALT;
	H.VirtualKey = VK_F3;
	return H;
}

inline HotkeyConfig default_stream_hotkey()
{
	HotkeyConfig H = {};
	H.Modifiers = HOTKEY_MOD_ALT;
	H.VirtualKey = VK_F2;
	return H;
}

inline HotkeyConfig default_load_model_hotkey()
{
	HotkeyConfig H = {};
	H.Modifiers = HOTKEY_MOD_ALT;
	H.VirtualKey = VK_F1;
	return H;
}

// ---------------------------------------------------------------------------
// Sound Config
// ---------------------------------------------------------------------------
#define SOUND_DEFAULT_START_FREQ   1000
#define SOUND_DEFAULT_STOP_FREQ    800
#define SOUND_DEFAULT_CANCEL_FREQ  400
#define SOUND_DEFAULT_VOLUME       50
#define SOUND_MIN_FREQ             200
#define SOUND_MAX_FREQ             2000
#define SOUND_START_DURATION_MS    200
#define SOUND_STOP_DURATION_MS     200
#define SOUND_CANCEL_DURATION_MS   300

struct SoundConfig
{
	int FreqHz;
	int Volume;
};

struct HotkeyCaptureState
{
	HotkeyConfig Captured;
	bool         HasCapture;
	bool         IsCapturing;
	UINT         PeakModifiers;
	UINT         PeakVirtualKey;
	int          ReleaseFrames;
};

struct SettingsWindowState
{
	int SelectedAction;
	HotkeyCaptureState Capture;
	HotkeyConfig TempHotkeys[4];
	bool TempPlayRecordSound;
	SoundConfig TempStartSound;
	SoundConfig TempStopSound;
	SoundConfig TempCancelSound;
	bool TempUseCharByCharInjection;
	int TempWhisperThreadCount;
};


// ---------------------------------------------------------------------------
// Button styles
// ---------------------------------------------------------------------------
#include "imgui.h"
#define BUTTON_COLOR_GREEN   ImVec4(0.0f, 0.50f, 0.0f, 1.0f)
#define BUTTON_COLOR_RED     ImVec4(0.75f, 0.07f, 0.13f, 1.0f)
#define BUTTON_COLOR_GREY    ImVec4(0.50f, 0.50f, 0.50f, 1.0f)
#define BUTTON_COLOR_BLUE    ImVec4(0.13f, 0.59f, 0.95f, 1.0f)

// ---------------------------------------------------------------------------
// Application State
// ---------------------------------------------------------------------------
struct GlobalState
{
	// Hotkeys
	HotkeyConfig RecordHotkey;
	HotkeyConfig CancelRecordHotkey;
	HotkeyConfig StreamHotkey;
	HotkeyConfig LoadModelHotkey;

	// Logic
	bool IsRecording;
	bool IsStreaming;
	bool IsSettingsDialogOpen;
	bool PlayRecordSound;
	SoundConfig StartSound;
	SoundConfig StopSound;
	SoundConfig CancelSound;
	bool UseCharByCharInjection;

	// Audio - platform-agnostic
	int CurrentAudioDeviceIndex;
	std::vector<AudioInputDeviceInfo> AudioInputDevices;

	// Inference Device
	int CurrentInferenceDeviceIndex;
	std::vector<std::string> InferenceDevices;

	// Whisper Wrapper
	int CurrentSTTModelIndex;
	std::vector<std::string> STTModelNames;
	std::vector<std::string> STTModelPaths;
	WhisperModelState WhisperState;

	// VAD model (absolute path, built at startup)
	std::string VadModelPath;

	// Audio capture pipeline
	std::atomic<bool> CaptureRunning;
	std::atomic<bool> CancelRequested;
	std::atomic<bool> PipelineActive;
	std::thread CaptureThread;
	std::mutex AudioBufferMutex;
	std::vector<float> AudioAccumBuffer;

	// Inference threading
	int WhisperThreadCount;

	// Our own main window handle — used to exclude self when doing just-in-time target lookup.
	HWND OwnWindow;
	SettingsWindowState SettingsState;

	// Toast notification
	std::string ToastMessage;
	double ToastExpireTime;

	// SystemInfo SystemInfo;
	// CPUInfo CpuInfo;
};

// ---------------------------------------------------------------------------
// Button idle-label helpers
// ---------------------------------------------------------------------------
inline std::string record_button_idle_label(GlobalState *AppState)
{
	return "Record (" + AppState->RecordHotkey.to_label() + ")";
}

inline std::string cancel_record_button_idle_label(GlobalState *AppState)
{
	return "Cancel (" + AppState->CancelRecordHotkey.to_label() + ")";
}

inline std::string stream_button_idle_label(GlobalState *AppState)
{
	return "Start Streaming (" + AppState->StreamHotkey.to_label() + ")";
}

inline std::string load_model_button_idle_label(GlobalState *AppState)
{
	return "Load Selected STT Model (" + AppState->LoadModelHotkey.to_label() + ")";
}
