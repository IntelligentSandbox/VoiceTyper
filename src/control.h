#pragma once

#include "state.h"
#include "audio_pipeline.h"

#ifdef _WIN32
	#include <windows.h>
#endif

inline
void
update_audio_input_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentAudioDeviceIndex = Index;
	#ifdef DEBUG
		printf("Selected audio input device index: %d\n", Index);
		if (Index >= 0 && Index < (int)AppState->AudioInputDevices.size())
		{
			AudioInputDeviceInfo Device = AppState->AudioInputDevices.at(Index);
			printf("Selected audio input device: %s\n", Device.Name.c_str());
		}
	#endif
}

inline
void
update_inference_device_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentInferenceDeviceIndex = Index;
	#ifdef DEBUG
		printf("Selected inference device index: %d\n", Index);
	#endif
}

inline
void
update_whisper_thread_count(GlobalState *AppState, int Count)
{
	if (Count < 1) Count = 1;
	AppState->WhisperThreadCount = Count;
	#ifdef DEBUG
		printf("[control] Whisper thread count set to: %d\n", Count);
	#endif
}

inline
void
update_stt_model_selection(GlobalState *AppState, int Index)
{
	AppState->CurrentSTTModelIndex = Index;

	#ifdef DEBUG
		printf("[control] Selected STT model index: %d\n", Index);
	#endif

	if (!is_whisper_model_loaded(&AppState->WhisperState)) return;
	if (AppState->WhisperState.LoadedModelIndex == Index) return;

	#ifdef DEBUG
		printf("[control] Model index changed while loaded (%d -> %d), reloading\n",
			AppState->WhisperState.LoadedModelIndex, Index);
	#endif

	AppState->LoadModelButton->setEnabled(false);
	AppState->LoadModelButton->setText("Loading...");
	AppState->LoadModelButton->repaint();
	QApplication::processEvents();

	bool Success = load_whisper_model(&AppState->WhisperState, Index);
	AppState->LoadModelButton->setEnabled(true);
	if (Success)
	{
		AppState->LoadModelButton->setStyleSheet(BUTTON_STYLE_BLUE);
		AppState->LoadModelButton->setText(
			QString("Unload STT Model (%1)").arg(AppState->LoadModelHotkey.to_label()));
		#ifdef DEBUG
			printf("[control] Model reloaded successfully\n");
		#endif
	}
	else
	{
		AppState->LoadModelButton->setStyleSheet(BUTTON_STYLE_RED);
		AppState->LoadModelButton->setText("Failed to load Model.");
		#ifdef DEBUG
			printf("[control] ERROR: Failed to reload model\n");
		#endif
	}
}

inline
void
toggle_recording(GlobalState *AppState)
{
	if (!is_whisper_model_loaded(&AppState->WhisperState))
	{
		#ifdef DEBUG
			printf("[control] toggle_recording: no model loaded, ignoring\n");
		#endif
		return;
	}

	if (AppState->IsStreaming)
	{
		#ifdef DEBUG
			printf("[control] toggle_recording: streaming is active, ignoring\n");
		#endif
		return;
	}

	AppState->IsRecording = !AppState->IsRecording;

	if (AppState->IsRecording)
	{
		bool Started = start_record_pipeline(AppState);
		if (!Started)
		{
			AppState->IsRecording = false;
			#ifdef DEBUG
				printf("[control] toggle_recording: failed to start record pipeline\n");
			#endif
			return;
		}
		if (AppState->PlayRecordSound) play_start_recording_sound();

		AppState->RecordButton->setStyleSheet(BUTTON_STYLE_RED);
		AppState->RecordButton->setText(QString("Stop (%1)").arg(AppState->RecordHotkey.to_label()));
		AppState->CancelRecordButton->setEnabled(true);
		AppState->StreamButton->setEnabled(false);
		AppState->StreamButton->setStyleSheet(BUTTON_STYLE_GREY);
	}
	else
	{
		if (AppState->PlayRecordSound) play_stop_recording_sound();
		signal_record_stop(AppState);
		AppState->RecordButton->setEnabled(false);
		AppState->RecordButton->setStyleSheet(BUTTON_STYLE_GREY);
		AppState->RecordButton->setText("Transcribing...");
		AppState->CancelRecordButton->setEnabled(false);
	}

	#ifdef DEBUG
		qDebug() << "Recording toggled to:" << AppState->IsRecording;
	#endif
}

inline
void
cancel_recording(GlobalState *AppState)
{
	if (!AppState->IsRecording) return;

	#ifdef DEBUG
		printf("[control] cancel_recording: cancelling\n");
	#endif

	AppState->CancelRequested.store(true);
	signal_record_stop(AppState);
	if (AppState->PlayRecordSound) play_cancel_recording_sound();

	AppState->IsRecording = false;
	AppState->RecordButton->setEnabled(false);
	AppState->RecordButton->setStyleSheet(BUTTON_STYLE_GREY);
	AppState->RecordButton->setText("Cancelled");
	AppState->CancelRecordButton->setEnabled(false);
}

inline
void
toggle_streaming(GlobalState *AppState)
{
	if (!is_whisper_model_loaded(&AppState->WhisperState))
	{
		#ifdef DEBUG
			printf("[control] toggle_streaming: no model loaded, ignoring\n");
		#endif
		return;
	}

	if (AppState->IsRecording)
	{
		#ifdef DEBUG
			printf("[control] toggle_streaming: recording is active, ignoring\n");
		#endif
		return;
	}

	AppState->IsStreaming = !AppState->IsStreaming;

	if (AppState->IsStreaming)
	{
		bool Started = start_streaming_pipeline(AppState);
		if (!Started)
		{
			AppState->IsStreaming = false;
			#ifdef DEBUG
				printf("[control] toggle_streaming: failed to start streaming pipeline\n");
			#endif
			return;
		}
		AppState->StreamButton->setStyleSheet(BUTTON_STYLE_RED);
		AppState->StreamButton->setText(QString("Stop Streaming (%1)").arg(AppState->StreamHotkey.to_label()));
		AppState->RecordButton->setEnabled(false);
		AppState->RecordButton->setStyleSheet(BUTTON_STYLE_GREY);
	}
	else
	{
		stop_streaming_pipeline(AppState);
		AppState->StreamButton->setStyleSheet(BUTTON_STYLE_GREEN);
		AppState->StreamButton->setText(stream_button_idle_label(AppState));
		AppState->RecordButton->setEnabled(true);
		AppState->RecordButton->setStyleSheet(BUTTON_STYLE_GREEN);
		AppState->RecordButton->setText(record_button_idle_label(AppState));
	}

	#ifdef DEBUG
		qDebug() << "Streaming toggled to:" << AppState->IsStreaming;
	#endif
}

inline
void
toggle_stt_model_load(GlobalState *AppState)
{
	#ifdef DEBUG
		printf("[control] toggle_stt_model_load called\n");
	#endif

	if (is_whisper_model_loaded(&AppState->WhisperState))
	{
		#ifdef DEBUG
			printf("[control] Unloading STT model...\n");
		#endif
		unload_whisper_model(&AppState->WhisperState);

		AppState->LoadModelButton->setStyleSheet(BUTTON_STYLE_GREY);
		AppState->LoadModelButton->setText(load_model_button_idle_label(AppState));

		#ifdef DEBUG
			printf("[control] STT model unloaded, button updated to grey\n");
		#endif
	}
	else
	{
		int ModelIdx = AppState->CurrentSTTModelIndex;
		if (ModelIdx < 0 || ModelIdx >= (int)AppState->STTModelAvailable.size() ||
			!AppState->STTModelAvailable[ModelIdx])
		{
			#ifdef DEBUG
				printf("[control] toggle_stt_model_load: selected model not available on disk\n");
			#endif
			return;
		}

		AppState->LoadModelButton->setEnabled(false);
		AppState->LoadModelButton->setText("Loading...");
		AppState->LoadModelButton->repaint();

		bool Success = load_whisper_model(&AppState->WhisperState, ModelIdx);
		AppState->LoadModelButton->setEnabled(true);
		if (Success)
		{
			AppState->LoadModelButton->setStyleSheet(BUTTON_STYLE_BLUE);
			AppState->LoadModelButton->setText(
				QString("Unload STT Model (%1)").arg(AppState->LoadModelHotkey.to_label()));
			#ifdef DEBUG
				printf("[control] STT model loaded successfully, button updated to blue\n");
			#endif
		}
		else
		{
			AppState->LoadModelButton->setStyleSheet(BUTTON_STYLE_RED);
			AppState->LoadModelButton->setText("Failed to load Model.");
			#ifdef DEBUG
				printf("[control] ERROR: Failed to load STT model\n");
			#endif
		}

	}
}
