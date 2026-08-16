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
#define SOUND_DEFAULT_START_FREQ   880
#define SOUND_DEFAULT_STOP_FREQ    659
#define SOUND_DEFAULT_CANCEL_FREQ  330
#define SOUND_DEFAULT_VOLUME       50
#define SOUND_MIN_FREQ             200
#define SOUND_MAX_FREQ             2000
#define SOUND_START_DURATION_MS    200
#define SOUND_STOP_DURATION_MS     200
#define SOUND_CANCEL_DURATION_MS   300
#define SOUND_PREVIEW_DURATION_MS  120

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
	double LastPreviewTime;
	char FontNameBuffer[128];
	bool FontNameBufferInitialized;
};

struct ModelDownloadState
{
	std::atomic<bool> IsRunning;
	std::atomic<bool> CancelRequested;
	std::atomic<bool> Succeeded;
	std::atomic<bool> Failed;
	std::atomic<int64_t> DownloadedBytes;
	std::atomic<int64_t> TotalBytes;
#ifndef _WIN32
	std::atomic<int64_t> ChildPid;
#endif

	std::string CurrentModelName;
	bool JustFinished;
	bool IsModalOpen;
	bool WantsOverwriteConfirm;
	std::string PendingModelName;
	std::string PendingUrl;
	std::string PendingDestPath;
	int64_t PendingSize;

	std::thread Thread;

	ModelDownloadState() :
		IsRunning(false),
		CancelRequested(false),
		Succeeded(false),
		Failed(false),
		DownloadedBytes(0),
		TotalBytes(0),
#ifndef _WIN32
		ChildPid(0),
#endif
		JustFinished(false),
		IsModalOpen(false),
		WantsOverwriteConfirm(false),
		PendingSize(0)
	{}

	ModelDownloadState(const ModelDownloadState &) = delete;
	ModelDownloadState &operator=(const ModelDownloadState &) = delete;
};

struct UpdateAssetInfo
{
	std::string Name;
	std::string Url;
	int64_t Size;
};

struct UpdateState
{
	std::atomic<bool> CheckRunning;
	std::atomic<bool> CheckSucceeded;
	std::atomic<bool> CheckFailed;
	std::atomic<bool> DownloadRunning;
	std::atomic<bool> DownloadCancelRequested;
	std::atomic<bool> DownloadSucceeded;
	std::atomic<bool> DownloadFailed;
	std::atomic<int64_t> DownloadedBytes;
	std::atomic<int64_t> TotalBytes;
#ifndef _WIN32
	std::atomic<int64_t> ChildPid;
#endif

	std::string LatestVersion;
	std::string ReleaseUrl;
	std::vector<UpdateAssetInfo> Assets;
	bool IsNewerAvailable;
	bool CheckJustFinished;
	bool DownloadJustFinished;
	bool ApplyOnDownload;

	UpdateAssetInfo PendingAsset;
	std::string DownloadDestPath;

	std::thread Thread;

	UpdateState() :
		CheckRunning(false),
		CheckSucceeded(false),
		CheckFailed(false),
		DownloadRunning(false),
		DownloadCancelRequested(false),
		DownloadSucceeded(false),
		DownloadFailed(false),
		DownloadedBytes(0),
		TotalBytes(0),
#ifndef _WIN32
		ChildPid(0),
#endif
		IsNewerAvailable(false),
		CheckJustFinished(false),
		DownloadJustFinished(false),
		ApplyOnDownload(false)
	{}

	UpdateState(const UpdateState &) = delete;
	UpdateState &operator=(const UpdateState &) = delete;
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
	std::atomic<bool> ExitRequested = false;
	bool PlayRecordSound;
	int StartSoundFreq;
	int StopSoundFreq;
	int CancelSoundFreq;
	bool UseCharByCharInjection;
	bool CopyToClipboardWhenNoTarget;
	bool ShowTranscribedTextConfidence;

	// Audio - platform-agnostic
	int CurrentAudioDeviceIndex;
	std::vector<AudioInputDeviceInfo> AudioInputDevices;
	std::vector<std::string> AudioInputDeviceNames;

	// Inference Device
	int CurrentInferenceDeviceIndex;
	std::vector<std::string> InferenceDevices;
	std::atomic<bool> InferenceDevicesLoaded = false;
	std::atomic<bool> InferenceDevicesLoading = false;
	std::thread InferenceDevicesThread;
	std::string PendingInferenceDeviceName;
	bool InferenceDevicePrefersCpu;

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
	std::atomic<bool> StreamingFinalizeOnStop;
	std::atomic<int> ModelTransitionFailureCode;
	std::thread CaptureThread;
	std::thread ModelTransitionThread;
	std::mutex AudioBufferMutex;
	std::vector<float> AudioAccumBuffer;

	// Inference threading
	int WhisperThreadCount;

	// UI font
	std::string UiFontName;
	int UiFontSize;

	// Latest operation timings (milliseconds). -1.0 means "no measurement yet".
	// Written from worker threads, read from the UI thread.
	std::atomic<double> LastModelLoadMs;
	std::atomic<double> LastTranscriptionMs;
	std::atomic<double> LastPasteMs;
};

struct UiRuntimeState
{
	SettingsWindowState SettingsState;
	ModelDownloadState Download;
	UpdateState Update;
	std::string ToastMessage;
	double ToastExpireTime;
	ColorRgba ToastBackgroundColor;
	int ToastSerial; // if user overflows this they need a life (but will never happen bc no one will use this slopapp but me.)
	bool IsCrashDialogOpen;
	bool CrashDialogOpened;
	std::vector<std::string> PendingCrashDumps;
	std::mutex TranscribedTextMutex;
	std::vector<TranscribedWord> TranscribedTextWords;
	int TranscribedTextSerial;
	std::vector<TranscribedWord> TranscribedTextBoxWords;
	std::vector<char> TranscribedTextBoxBuffer;
	int TranscribedTextBoxSerial;
	bool FontReloadRequested;
};

struct GlobalState : CoreRuntimeState
{
	UiRuntimeState Ui;
	PlatformRuntimeState Platform;
};
