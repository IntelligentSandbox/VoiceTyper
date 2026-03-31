#pragma once

#ifndef VOICETYPER_USE_IMGUI
	#include "qt.h"
#endif

#include "whisper_wrapper.h"

#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <string>

#ifdef VOICETYPER_USE_IMGUI
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

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

#define WINDOW_DEFAULT_WIDTH 500
#define WINDOW_DEFAULT_HEIGHT 500

#define APP_ICON_PATH "media/voicetyper-icon.png"

// ---------------------------------------------------------------------------
// Hotkey Config
// ---------------------------------------------------------------------------
#ifdef VOICETYPER_USE_IMGUI

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

#else // Qt build

// Represents a user-configurable hotkey as Qt modifier flags + an optional main key.
// Key == Qt::Key_unknown (0) means the hotkey fires on modifiers alone (e.g. Ctrl+Alt).
struct HotkeyConfig
{
	Qt::KeyboardModifiers Modifiers;
	Qt::Key               Key;

	// Returns a human-readable label like "Ctrl+Alt" or "Ctrl+Shift+R"
	QString to_label() const
	{
		QKeySequence Seq(Modifiers | Key);
		if (Key == Qt::Key_unknown || Key == 0)
		{
			// Build modifier-only label manually
			QStringList Parts;
			if (Modifiers & Qt::ControlModifier) Parts << "Ctrl";
			if (Modifiers & Qt::AltModifier)     Parts << "Alt";
			if (Modifiers & Qt::ShiftModifier)   Parts << "Shift";
			if (Modifiers & Qt::MetaModifier)    Parts << "Meta";
			return Parts.join("+");
		}
		return Seq.toString(QKeySequence::NativeText);
	}

	bool is_valid() const
	{
		return Modifiers != Qt::NoModifier || (Key != Qt::Key_unknown && Key != 0);
	}
};

inline HotkeyConfig default_record_hotkey()
{
	HotkeyConfig H;
	H.Modifiers = Qt::ControlModifier | Qt::AltModifier;
	H.Key       = Qt::Key(0);
	return H;
}

inline HotkeyConfig default_cancel_record_hotkey()
{
	HotkeyConfig H;
	H.Modifiers = Qt::AltModifier;
	H.Key       = Qt::Key_F3;
	return H;
}

inline HotkeyConfig default_stream_hotkey()
{
	HotkeyConfig H;
	H.Modifiers = Qt::AltModifier;
	H.Key       = Qt::Key_F2;
	return H;
}

inline HotkeyConfig default_load_model_hotkey()
{
	HotkeyConfig H;
	H.Modifiers = Qt::AltModifier;
	H.Key       = Qt::Key_F1;
	return H;
}

#endif // VOICETYPER_USE_IMGUI

// ---------------------------------------------------------------------------
// Button styles
// ---------------------------------------------------------------------------
#ifdef VOICETYPER_USE_IMGUI

#include "imgui.h"
#define BUTTON_COLOR_GREEN   ImVec4(0.0f, 0.50f, 0.0f, 1.0f)
#define BUTTON_COLOR_RED     ImVec4(0.75f, 0.07f, 0.13f, 1.0f)
#define BUTTON_COLOR_GREY    ImVec4(0.50f, 0.50f, 0.50f, 1.0f)
#define BUTTON_COLOR_BLUE    ImVec4(0.13f, 0.59f, 0.95f, 1.0f)

#else

#define BUTTON_STYLE_GREEN "font-family: Georgia; font-size: 12pt; font-weight: bold; color: black; background-color: green;"
#define BUTTON_STYLE_RED "font-family: Georgia; font-size: 12pt; font-weight: bold; color: black; background-color: #bF1121;"
#define BUTTON_STYLE_GREY "font-family: Georgia; font-size: 12pt; font-weight: bold; color: black; background-color: #808080;"
#define BUTTON_STYLE_BLUE "font-family: Georgia; font-size: 12pt; font-weight: bold; color: white; background-color: #2196F3;"

#endif

// ---------------------------------------------------------------------------
// Application State
// ---------------------------------------------------------------------------
struct GlobalState
{
#ifndef VOICETYPER_USE_IMGUI
	// UI (Qt)
	QApplication *QtApp;
	QWidget *QtMainWindow;
	QPushButton *RecordButton;
	QPushButton *CancelRecordButton;
	QPushButton *StreamButton;
	QPushButton *LoadModelButton;
	QPushButton *SettingsButton;
	QComboBox *AudioInputDropdown;
	QComboBox *STTModelDropdown;
	QComboBox *InferenceDeviceDropdown;
#endif

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
	bool UseCharByCharInjection;

	// Audio - platform-agnostic
	int CurrentAudioDeviceIndex;
	std::vector<AudioInputDeviceInfo> AudioInputDevices;

	// Inference Device
	int CurrentInferenceDeviceIndex;
	std::vector<std::string> InferenceDevices;

	// Whisper Wrapper
	int CurrentSTTModelIndex;
	std::vector<std::string> STTModels;
	std::vector<bool> STTModelAvailable;
	bool AnySTTModelAvailable;
	WhisperModelState WhisperState;

	// Audio capture pipeline
	std::atomic<bool> CaptureRunning;
	std::atomic<bool> CancelRequested;
	std::thread CaptureThread;
	std::mutex AudioBufferMutex;
	std::vector<float> AudioAccumBuffer;

	// Inference threading
	int WhisperThreadCount;

	// Our own main window handle — used to exclude self when doing just-in-time target lookup.
#ifdef VOICETYPER_USE_IMGUI
	HWND OwnWindow;
#else
	void *OwnWindow;
#endif

	// SystemInfo SystemInfo;
	// CPUInfo CpuInfo;
};

// ---------------------------------------------------------------------------
// Button idle-label helpers
// ---------------------------------------------------------------------------
#ifdef VOICETYPER_USE_IMGUI

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

#else

inline QString record_button_idle_label(GlobalState *AppState)
{
	return QString("Record (%1)").arg(AppState->RecordHotkey.to_label());
}

inline QString cancel_record_button_idle_label(GlobalState *AppState)
{
	return QString("Cancel (%1)").arg(AppState->CancelRecordHotkey.to_label());
}

inline QString stream_button_idle_label(GlobalState *AppState)
{
	return QString("Start Streaming (%1)").arg(AppState->StreamHotkey.to_label());
}

inline QString load_model_button_idle_label(GlobalState *AppState)
{
	return QString("Load Selected STT Model (%1)").arg(AppState->LoadModelHotkey.to_label());
}

#endif
