#include "state.h"
#include "ui.h"
#include "system.h"
#include "audio_pipeline.h"

int main(int argc, char *argv[])
{
	#ifdef DEBUG
		if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
	#endif

	QApplication QtApp(argc, argv);
	QWidget QtMainWindow;

	GlobalState AppState;
	AppState.QtApp = &QtApp;
	AppState.QtMainWindow = &QtMainWindow;

	AppState.IsRecording   = false;
	AppState.IsStreaming   = false;
	AppState.CaptureRunning = false;
	AppState.OwnWindow = nullptr;
	AppState.SettingsButton = nullptr;
	AppState.AudioInputDropdown = nullptr;
	AppState.InferenceDeviceDropdown = nullptr;
	AppState.IsSettingsDialogOpen = false;
	AppState.PlayRecordSound = false;

	init_whisper_state(&AppState.WhisperState);

	query_audio_input_devices(&AppState);
	query_inference_devices(&AppState);
	query_available_stt_models(&AppState);
	query_whisper_thread_count(&AppState);
	query_hotkey_settings(&AppState);

	start_hotkey_listener(&AppState);

	init_ui(&AppState);

	QtMainWindow.show();
	AppState.OwnWindow = (HWND)QtMainWindow.winId();

	set_application_icon(&AppState);
	#ifdef _WIN32
		set_taskbar_icon(AppState.OwnWindow, APP_ICON_PATH);
	#endif
	int exitCode = QtApp.exec();

	stop_hotkey_listener();

	if (AppState.IsRecording) signal_record_stop(&AppState);
	if (AppState.IsStreaming) stop_streaming_pipeline(&AppState);

	if (is_whisper_model_loaded(&AppState.WhisperState))
	{
		unload_whisper_model(&AppState.WhisperState);
		#ifdef DEBUG
			printf("[main] Unloaded whisper model on exit\n");
		#endif
	}
	
	return exitCode;
}
