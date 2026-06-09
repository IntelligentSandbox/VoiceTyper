#pragma once

#include "runtime_types.h"
#include "whisper_wrapper.h"

#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <string>

#define MAX_AUDIO_DEVICE_NAME_LENGTH 512

#define AUDIO_CAPTURE_SAMPLE_RATE       16000
#define AUDIO_CAPTURE_CHANNELS          1
#define AUDIO_CAPTURE_BITS_PER_SAMPLE   16
#define AUDIO_CAPTURE_BUFFER_MS         100
#define AUDIO_CAPTURE_BUFFER_COUNT      8

#define WINDOW_DEFAULT_WIDTH 700
#define WINDOW_DEFAULT_HEIGHT 575

#define APP_ICON_PATH "media/voicetyper-icon.png"

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

struct HotkeyCaptureState
{
	HotkeyConfig Captured;
	bool         HasCapture;
	bool         IsCapturing;
	AppHotkeyModifiers PeakModifiers;
	AppKeyCode         PeakVirtualKey;
	int          ReleaseFrames;
};

struct SettingsWindowState
{
	int SelectedAction;
	HotkeyCaptureState Capture;
	HotkeyConfig TempHotkeys[4];
	RecordingHotkeyMode TempRecordHotkeyMode;
	bool TempPlayRecordSound;
	SoundConfig TempStartSound;
	SoundConfig TempStopSound;
	SoundConfig TempCancelSound;
	bool TempUseCharByCharInjection;
	int TempWhisperThreadCount;
	char TempStartSoundFreqText[16];
	char TempStartSoundVolumeText[16];
	char TempStopSoundFreqText[16];
	char TempStopSoundVolumeText[16];
	char TempCancelSoundFreqText[16];
	char TempCancelSoundVolumeText[16];
};

// ---------------------------------------------------------------------------
// Application State
// ---------------------------------------------------------------------------
struct CoreRuntimeState
{
	// Hotkeys
	HotkeyConfig RecordHotkey;
	HotkeyConfig CancelRecordHotkey;
	HotkeyConfig StreamHotkey;
	HotkeyConfig LoadModelHotkey;
	RecordingHotkeyMode RecordHotkeyMode;

	// Logic
	bool IsRecording;
	bool IsStreaming;
	std::atomic<bool> IsModelTransitioning;
	bool PlayRecordSound;
	SoundConfig StartSound;
	SoundConfig StopSound;
	SoundConfig CancelSound;
	bool UseCharByCharInjection;

	// Audio - platform-agnostic
	int CurrentAudioDeviceIndex;
	std::vector<AudioInputDeviceInfo> AudioInputDevices;
	std::vector<std::string> AudioInputDeviceNames;

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
	std::atomic<int> ModelTransitionFailureCode;
	std::thread CaptureThread;
	std::thread ModelTransitionThread;
	std::mutex AudioBufferMutex;
	std::vector<float> AudioAccumBuffer;

	// Inference threading
	int WhisperThreadCount;
};

struct UiRuntimeState
{
	bool IsSettingsDialogOpen;
	SettingsWindowState SettingsState;
	std::string ToastMessage;
	double ToastExpireTime;
	ColorRgba ToastBackgroundColor;
	int ToastSerial; // if user overflows this they need a life (but will never happen bc no one will use this slopapp but me.)
};

struct GlobalState : CoreRuntimeState
{
	UiRuntimeState Ui;
	PlatformRuntimeState Platform;

	// SystemInfo SystemInfo;
	// CPUInfo CpuInfo;
};
