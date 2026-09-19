#pragma once

#include "audio_pool.h"
#include "build_time_constants.h"
#include "runtime_types.h"
#include "whisper_wrapper.h"

#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <string>

struct HotkeyCaptureState
{
	HotkeyConfig Captured;
	bool         HasCapture;
	bool         IsCapturing;
	bool         Arming;
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
	int FontSuggestionIndex;
	int FontSuggestionMatchCount;
	char WhisperPromptBuffer[512];
	bool WhisperPromptBufferInitialized;
	char NewPasteOverrideProcess[128];
	HotkeyCaptureState PasteOverrideCapture;
	std::string PasteOverrideCaptureProcess;
	bool HotkeysModalOpen;
};

struct ModelDownloadState
{
	std::atomic<bool> IsRunning;
	std::atomic<bool> CancelRequested;
	std::atomic<bool> Succeeded;
	std::atomic<bool> Failed;
	std::atomic<int64_t> DownloadedBytes;
	std::atomic<int64_t> TotalBytes;
	// Nonzero while a platform layer has delegated the download to an external
	// child process (e.g. curl under Linux); always 0 on platforms that
	// download in-process.
	std::atomic<int64_t> ChildPid;

	std::string CurrentModelName;
	bool JustFinished;
	bool IsModalOpen;
	bool WantsOverwriteConfirm;
	float ModalWidth;
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
		ChildPid(0),
		JustFinished(false),
		IsModalOpen(false),
		WantsOverwriteConfirm(false),
		ModalWidth(0.0f),
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

struct UpdateChangelogEntry
{
	std::string Version;
	std::string Notes;
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
	// Nonzero while a platform layer has delegated the download to an external
	// child process (e.g. curl under Linux); always 0 on platforms that
	// download in-process.
	std::atomic<int64_t> ChildPid;

	std::string LatestVersion;
	std::string ReleaseUrl;
	std::vector<UpdateAssetInfo> Assets;
	std::vector<UpdateChangelogEntry> NewerReleases;
	bool IsNewerAvailable;
	bool CheckJustFinished;
	bool DownloadJustFinished;
	bool ApplyOnDownload;
	bool IsModalOpen;

	std::string StagingLatestVersion;
	std::string StagingReleaseUrl;
	std::vector<UpdateAssetInfo> StagingAssets;
	std::vector<UpdateChangelogEntry> StagingNewerReleases;
	bool StagingIsNewerAvailable;
	bool StagingCheckSucceeded;
	bool ThreadIsCheck;

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
		ChildPid(0),
		IsNewerAvailable(false),
		CheckJustFinished(false),
		DownloadJustFinished(false),
		ApplyOnDownload(false),
		IsModalOpen(false),
		StagingIsNewerAvailable(false),
		StagingCheckSucceeded(false),
		ThreadIsCheck(false)
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
	HotkeyConfig PasteHotkey;
	HotkeyConfig FontSizeUpHotkey;
	HotkeyConfig FontSizeDownHotkey;
	RecordingHotkeyMode RecordHotkeyMode;

	// Per-program paste hotkey overrides, matched against the target window's
	// process (executable) name. Guarded by PasteHotkeyOverridesMutex: written
	// by the UI thread, copied by pipeline threads at paste time.
	std::vector<PasteHotkeyOverride> PasteHotkeyOverrides;
	std::mutex PasteHotkeyOverridesMutex;

	// Logic
	bool IsRecording;
	bool IsStreaming;
	bool PendingRecordOnModelLoad;
	bool PendingStreamOnModelLoad;
	std::atomic<bool> IsModelTransitioning;
	std::atomic<bool> ExitRequested = false;
	bool PlayRecordSound;
	bool ShowRecordIndicator;
	int RecordIndicatorDelayMs;
	int StartSoundFreq;
	int StopSoundFreq;
	int CancelSoundFreq;
	bool UseCharByCharInjection;
	bool CopyToClipboardWhenNoTarget;
	bool PreserveClipboardOnPaste;
	int ClipboardRestoreDelayMs;

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
	AudioBlockPool AudioPool;
	AudioClip AudioAccum;
	// Contiguous whisper-input buffer, gathered from AudioAccum blocks by
	// whichever pipeline thread runs inference (they never overlap).
	std::vector<float> WhisperStaging;

	// Inference threading
	int WhisperThreadCount;

	// Whisper initial prompt (vocabulary/style hint). Guarded by
	// WhisperInitialPromptMutex: written by the UI thread, copied by pipeline
	// threads when they build their whisper_full_params.
	std::string WhisperInitialPrompt;
	std::mutex WhisperInitialPromptMutex;

	// UI font
	std::string UiFontName;
	std::vector<std::string> UiFontNames;
	int UiFontSize;

	// Latest operation timings (milliseconds). -1.0 means "no measurement yet".
	// Written from worker threads, read from the UI thread.
	std::atomic<double> LastModelLoadMs;
	std::atomic<double> LastTranscriptionMs;
	std::atomic<double> LastPasteMs;

	// Record/stream pipeline start latency breakdown. PipelineRequestNs is the
	// perf-timeline (see perf.h) instant the UI thread requested the pipeline;
	// the capture thread fills the Last* values (ms, -1.0 = no measurement yet)
	// relative to it: device opened -> capture actually started -> first audio
	// samples appended to the accumulator.
	std::atomic<int64_t> PipelineRequestNs;
	std::atomic<double> LastRecordDeviceOpenMs;
	std::atomic<double> LastRecordCaptureStartMs;
	std::atomic<double> LastRecordFirstAudioMs;
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
	bool LightMode;
};

struct GlobalState : CoreRuntimeState
{
	UiRuntimeState Ui;
	PlatformRuntimeState Platform;
};
