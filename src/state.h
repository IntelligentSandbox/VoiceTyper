#pragma once

#include "qt.h"
#include "platform_win32.h"
#include "whisper_wrapper.h"

#include <atomic>
#include <mutex>
#include <vector>
#include <thread>

#define WINDOW_DEFAULT_WIDTH 500
#define WINDOW_DEFAULT_HEIGHT 500

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

#define BUTTON_STYLE_GREEN "font-family: Georgia; font-size: 12pt; font-weight: bold; color: black; background-color: green;"
#define BUTTON_STYLE_RED "font-family: Georgia; font-size: 12pt; font-weight: bold; color: black; background-color: #bF1121;"
#define BUTTON_STYLE_GREY "font-family: Georgia; font-size: 12pt; font-weight: bold; color: black; background-color: #808080;"
#define BUTTON_STYLE_BLUE "font-family: Georgia; font-size: 12pt; font-weight: bold; color: white; background-color: #2196F3;"

struct GlobalState
{
	// UI
	QApplication *QtApp;
	QWidget *QtMainWindow;
	QPushButton *RecordButton;
	QPushButton *StreamButton;
	QPushButton *LoadModelButton;
	QPushButton *SettingsButton;

	// Hotkeys
	HotkeyConfig RecordHotkey;
	HotkeyConfig StreamHotkey;
	HotkeyConfig LoadModelHotkey;

	// Logic
	bool IsRecording;
	bool IsStreaming;
	bool IsSettingsDialogOpen;
	bool PlayRecordSound;

	// Audio - platform-agnostic
	int CurrentAudioDeviceIndex;
	std::vector<AudioInputDeviceInfo> AudioInputDevices;

	// Inference Device 
	int CurrentInferenceDeviceIndex;
	std::vector<std::string> InferenceDevices;

	// Whisper Wrapper
	int CurrentSTTModelIndex;
	std::vector<std::string> STTModels;
	WhisperModelState WhisperState;

	// Audio capture pipeline
	std::atomic<bool> CaptureRunning;
	std::thread CaptureThread;
	std::mutex AudioBufferMutex;
	std::vector<float> AudioAccumBuffer;

	// Inference threading
	int WhisperThreadCount;

	// Our own main window handle — used to exclude self when doing just-in-time target lookup.
	HWND OwnWindow;

	// SystemInfo SystemInfo;
	// CPUInfo CpuInfo;
};

// ---- Button idle-label helpers ----
// Centralized here so both control.h and audio_pipeline.h can use them
// without a circular include dependency.

inline QString record_button_idle_label(GlobalState *AppState)
{
	return QString("Record (%1)").arg(AppState->RecordHotkey.to_label());
}

inline QString stream_button_idle_label(GlobalState *AppState)
{
	return QString("Start Streaming (%1)").arg(AppState->StreamHotkey.to_label());
}

inline QString load_model_button_idle_label(GlobalState *AppState)
{
	return QString("Load Selected STT Model (%1)").arg(AppState->LoadModelHotkey.to_label());
}
