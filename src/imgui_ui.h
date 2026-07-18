#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "state.h"
#include "input.h"
#include "settings.h"
#include "control.h"

#include <cstdio>

#define BUTTON_COLOR_GREEN   ImVec4(0.0f, 0.50f, 0.0f, 1.0f)
#define BUTTON_COLOR_RED     ImVec4(0.75f, 0.07f, 0.13f, 1.0f)
#define BUTTON_COLOR_GREY    ImVec4(0.50f, 0.50f, 0.50f, 1.0f)
#define BUTTON_COLOR_BLUE    ImVec4(0.13f, 0.59f, 0.95f, 1.0f)

// ---------------------------------------------------------------------------
// Styled button helper
// ---------------------------------------------------------------------------
static bool
colored_button(const char *Label, const ImVec2 &Size, const ImVec4 &Color, bool Enabled = true)
{
	if (!Enabled)
	{
		ImGui::PushStyleColor(ImGuiCol_Button,        BUTTON_COLOR_GREY);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  BUTTON_COLOR_GREY);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,   BUTTON_COLOR_GREY);
		ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		ImGui::BeginDisabled();
		bool Pressed = ImGui::Button(Label, Size);
		ImGui::EndDisabled();
		ImGui::PopStyleColor(4);
		return false;
	}

	ImVec4 Hovered = ImVec4(
		Color.x * 1.2f > 1.0f ? 1.0f : Color.x * 1.2f,
		Color.y * 1.2f > 1.0f ? 1.0f : Color.y * 1.2f,
		Color.z * 1.2f > 1.0f ? 1.0f : Color.z * 1.2f,
		Color.w);
	ImVec4 Active = ImVec4(
		Color.x * 0.8f,
		Color.y * 0.8f,
		Color.z * 0.8f,
		Color.w);

	ImGui::PushStyleColor(ImGuiCol_Button,        Color);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  Hovered);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,   Active);
	ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
	bool Pressed = ImGui::Button(Label, Size);
	ImGui::PopStyleColor(4);

	return Pressed;
}

// ---------------------------------------------------------------------------
// Combo helper for std::vector<std::string>
// ---------------------------------------------------------------------------
static bool
string_combo(const char *Label, int *CurrentIndex, const std::vector<std::string> &Items)
{
	if (Items.empty()) return false;

	int Idx = *CurrentIndex;
	if (Idx < 0 || Idx >= (int)Items.size()) Idx = 0;

	const char *Preview = Items[Idx].c_str();
	bool Changed = false;

	if (ImGui::BeginCombo(Label, Preview))
	{
		for (int i = 0; i < (int)Items.size(); i++)
		{
			bool IsSelected = (Idx == i);
			if (ImGui::Selectable(Items[i].c_str(), IsSelected))
			{
				*CurrentIndex = i;
				Changed = true;
			}
			if (IsSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	return Changed;
}

static std::string
record_button_idle_label(GlobalState *AppState)
{
	if (AppState->RecordHotkeyMode == RECORDING_HOTKEY_HOLD)
		return "Record (hold " + hotkey_to_label(AppState->RecordHotkey) + ")";

	return "Record (" + hotkey_to_label(AppState->RecordHotkey) + ")";
}

static std::string
cancel_record_button_idle_label(GlobalState *AppState)
{
	return "Cancel (" + hotkey_to_label(AppState->CancelRecordHotkey) + ")";
}

static std::string
stream_button_idle_label(GlobalState *AppState)
{
	return "Start Streaming (" + hotkey_to_label(AppState->StreamHotkey) + ")";
}

static std::string
load_model_button_idle_label(GlobalState *AppState)
{
	return "Load Selected STT Model (" + hotkey_to_label(AppState->LoadModelHotkey) + ")";
}

// ---------------------------------------------------------------------------
// Settings Dialog - select action
// ---------------------------------------------------------------------------
static void
settings_select_action(SettingsWindowState *S, int Action)
{
	S->SelectedAction = Action;
	S->Capture.Captured = S->TempHotkeys[Action];
	S->Capture.HasCapture = S->TempHotkeys[Action].is_valid();
	S->Capture.IsCapturing = false;
	S->Capture.PeakModifiers = 0;
	S->Capture.PeakVirtualKey = 0;
	S->Capture.ReleaseFrames = 0;
}

// ---------------------------------------------------------------------------
// Settings Dialog - initialize state
// ---------------------------------------------------------------------------
static void
init_settings_state(GlobalState *AppState)
{
	SettingsWindowState *S = &AppState->Ui.SettingsState;
	S->SelectedAction = 0;
	S->TempHotkeys[0] = AppState->RecordHotkey;
	S->TempHotkeys[1] = AppState->CancelRecordHotkey;
	S->TempHotkeys[2] = AppState->StreamHotkey;
	S->TempHotkeys[3] = AppState->LoadModelHotkey;
	S->TempRecordHotkeyMode = AppState->RecordHotkeyMode;
	S->TempPlayRecordSound = AppState->PlayRecordSound;
	S->TempStartSoundFreq = AppState->StartSoundFreq;
	S->TempStopSoundFreq = AppState->StopSoundFreq;
	S->TempCancelSoundFreq = AppState->CancelSoundFreq;
	S->TempUseCharByCharInjection = AppState->UseCharByCharInjection;
	S->TempWhisperThreadCount = AppState->WhisperThreadCount;
	S->LastPreviewTime = -1.0;
	S->Capture.Captured = AppState->RecordHotkey;
	S->Capture.HasCapture = AppState->RecordHotkey.is_valid();
	S->Capture.IsCapturing = false;
	S->Capture.PeakModifiers = 0;
	S->Capture.PeakVirtualKey = 0;
	S->Capture.ReleaseFrames = 0;
}

// ---------------------------------------------------------------------------
// Settings Dialog
// ---------------------------------------------------------------------------
static void
settings_preview_sound(GlobalState *AppState, SettingsWindowState *S, int FreqHz, bool Force)
{
	double Now = ImGui::GetTime();
	if (!Force && Now - S->LastPreviewTime < 0.15) return;
	platform_play_sound(&AppState->Platform, FreqHz, SOUND_PREVIEW_DURATION_MS);
	S->LastPreviewTime = Now;
}

static void
render_settings_ui(GlobalState *AppState)
{
	SettingsWindowState *S = &AppState->Ui.SettingsState;

	ImVec2 Display = ImGui::GetIO().DisplaySize;
	float SettingsW = (Display.x < 620.0f) ? Display.x : 620.0f;
	ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), Display);
	ImGui::SetNextWindowSize(ImVec2(SettingsW, 0));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
		return;

	AppState->Ui.IsSettingsDialogOpen = true;

#ifdef VOICETYPER_CUDA
	ImGui::TextDisabled("v%s CUDA", VOICETYPER_VERSION_FULL);
#else
	ImGui::TextDisabled("v%s CPU", VOICETYPER_VERSION_FULL);
#endif

	ImGui::Checkbox("Play sound when starting/stopping/cancelling recording",
		&S->TempPlayRecordSound);

	if (S->TempPlayRecordSound)
	{
		ImGui::Indent(20.0f);

		if (ImGui::BeginTable("##SoundSettings", 2,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Sound", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Pitch (200-2000 Hz)", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Start");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderInt("##StartPitch", &S->TempStartSoundFreq,
				SOUND_MIN_FREQ, SOUND_MAX_FREQ, "%d Hz"))
			{
				settings_preview_sound(AppState, S, S->TempStartSoundFreq, false);
			}
			if (ImGui::IsItemDeactivated())
			{
				settings_preview_sound(AppState, S, S->TempStartSoundFreq, true);
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Stop");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderInt("##StopPitch", &S->TempStopSoundFreq,
				SOUND_MIN_FREQ, SOUND_MAX_FREQ, "%d Hz"))
			{
				settings_preview_sound(AppState, S, S->TempStopSoundFreq, false);
			}
			if (ImGui::IsItemDeactivated())
			{
				settings_preview_sound(AppState, S, S->TempStopSoundFreq, true);
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Cancel");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderInt("##CancelPitch", &S->TempCancelSoundFreq,
				SOUND_MIN_FREQ, SOUND_MAX_FREQ, "%d Hz"))
			{
				settings_preview_sound(AppState, S, S->TempCancelSoundFreq, false);
			}
			if (ImGui::IsItemDeactivated())
			{
				settings_preview_sound(AppState, S, S->TempCancelSoundFreq, true);
			}

			ImGui::EndTable();
		}

		ImGui::Unindent(20.0f);
	}

	ImGui::Checkbox("Use character-by-character text injection (instead of paste Ctrl+Shift+V)",
		&S->TempUseCharByCharInjection);

	ImGui::Text("CPU Cores for Inference:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	int MaxCores = query_logical_processor_count();
	if (ImGui::InputInt("##ThreadCount", &S->TempWhisperThreadCount, 1, 1))
	{
		if (S->TempWhisperThreadCount < 1) S->TempWhisperThreadCount = 1;
		if (S->TempWhisperThreadCount > MaxCores) S->TempWhisperThreadCount = MaxCores;
	}

	if (colored_button("Copy Exe Dir Path", ImVec2(-1.0f, 0.0f), BUTTON_COLOR_GREY))
	{
		std::string ExeDir = platform_get_exe_dir();
		ImGui::SetClipboardText(ExeDir.c_str());
		show_success_toast(AppState, "Exe dir copied to clipboard!");
	}

	ImGui::Separator();
	ImGui::Text("Keyboard Shortcuts");

	ImGui::Text("Record Hotkey Mode");
	if (ImGui::RadioButton("Hold key to record",
		S->TempRecordHotkeyMode == RECORDING_HOTKEY_HOLD))
	{
		S->TempRecordHotkeyMode = RECORDING_HOTKEY_HOLD;
	}
	if (ImGui::RadioButton("Press key to start/stop",
		S->TempRecordHotkeyMode == RECORDING_HOTKEY_TOGGLE))
	{
		S->TempRecordHotkeyMode = RECORDING_HOTKEY_TOGGLE;
	}

	float AvailWidth = ImGui::GetContentRegionAvail().x;
	float Spacing = ImGui::GetStyle().ItemSpacing.x;
	float BtnWidth = (AvailWidth - Spacing * 3) / 4;
	ImVec2 ActionSize = ImVec2(BtnWidth, 40);

	const char *ActionLabels[] = { "Record", "Cancel Record", "Stream", "Load Model" };
	for (int i = 0; i < 4; i++)
	{
		if (i > 0) ImGui::SameLine();
		ImVec4 Color = (S->SelectedAction == i) ? BUTTON_COLOR_BLUE : BUTTON_COLOR_GREY;
		if (colored_button(ActionLabels[i], ActionSize, Color))
			settings_select_action(S, i);
	}

	ImGui::Text("Current: %s", hotkey_to_label(S->TempHotkeys[S->SelectedAction]).c_str());

	if (S->Capture.IsCapturing)
	{
		ImGui::SetNextFrameWantCaptureKeyboard(true);
		ImGui::ClearActiveID();

		if (app_key_is_down(APP_KEY_ESCAPE))
		{
			S->Capture.HasCapture = false;
			S->Capture.Captured = {};
			S->TempHotkeys[S->SelectedAction] = {};
			S->Capture.IsCapturing = false;
			S->Capture.PeakModifiers = 0;
			S->Capture.PeakVirtualKey = 0;
			S->Capture.ReleaseFrames = 0;
			ImGui::ClearActiveID();
			ImGui::SetNextFrameWantCaptureKeyboard(true);
		}
		else
		{
			AppHotkeyModifiers Mods = poll_modifier_state();
			AppKeyCode Vk = poll_nonmodifier_key();

			if (Mods != 0 || Vk != APP_KEY_NONE)
			{
				S->Capture.PeakModifiers |= Mods;
				if (Vk != APP_KEY_NONE) S->Capture.PeakVirtualKey = Vk;
				S->Capture.ReleaseFrames = 0;

				S->Capture.Captured.Modifiers = S->Capture.PeakModifiers;
				S->Capture.Captured.VirtualKey = S->Capture.PeakVirtualKey;
				S->Capture.HasCapture = true;
				S->TempHotkeys[S->SelectedAction] = S->Capture.Captured;
			}
			else if (S->Capture.PeakModifiers != 0 || S->Capture.PeakVirtualKey != 0)
			{
				S->Capture.ReleaseFrames++;
				if (S->Capture.ReleaseFrames >= 10)
				{
					S->Capture.Captured.Modifiers = S->Capture.PeakModifiers;
					S->Capture.Captured.VirtualKey = S->Capture.PeakVirtualKey;
					S->Capture.HasCapture = true;
					S->TempHotkeys[S->SelectedAction] = S->Capture.Captured;
					S->Capture.IsCapturing = false;
					S->Capture.PeakModifiers = 0;
					S->Capture.PeakVirtualKey = 0;
					S->Capture.ReleaseFrames = 0;
					ImGui::ClearActiveID();
					ImGui::SetNextFrameWantCaptureKeyboard(true);
				}
			}
		}
	}

	// Capture display button
	{
		std::string CaptureText;
		ImVec4 BgColor;

		if (S->Capture.IsCapturing)
		{
			BgColor = ImVec4(0.08f, 0.40f, 0.75f, 1.0f);
			if (S->Capture.HasCapture)
				CaptureText = hotkey_to_label(S->Capture.Captured) + "...";
			else
				CaptureText = "Press a key combination...";
		}
		else
		{
			BgColor = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
			if (S->Capture.HasCapture)
				CaptureText = hotkey_to_label(S->Capture.Captured);
			else
				CaptureText = "Click here, then press your hotkey...";
		}

		ImGui::PushStyleColor(ImGuiCol_Button, BgColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
			ImVec4(BgColor.x * 1.3f > 1.0f ? 1.0f : BgColor.x * 1.3f,
			       BgColor.y * 1.3f > 1.0f ? 1.0f : BgColor.y * 1.3f,
			       BgColor.z * 1.3f > 1.0f ? 1.0f : BgColor.z * 1.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, BgColor);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

		std::string ButtonLabel = CaptureText + "##CaptureHotkey";
		if (ImGui::Button(ButtonLabel.c_str(), ImVec2(-1, 40)))
		{
			S->Capture.IsCapturing = !S->Capture.IsCapturing;
			if (S->Capture.IsCapturing)
			{
				S->Capture.PeakModifiers = 0;
				S->Capture.PeakVirtualKey = 0;
				S->Capture.ReleaseFrames = 0;
			}
		}

		ImGui::PopStyleColor(4);
	}

	ImGui::TextWrapped(
		"Select an action above, then click the box and press your desired combination. "
		"Modifier-only combos (e.g. Ctrl+Alt) are supported. Escape clears the selected shortcut.");

	ImGui::Separator();

	float BottomBtnWidth = (AvailWidth - Spacing) / 2;
	ImVec2 BottomSize = ImVec2(BottomBtnWidth, 40);

	if (colored_button("Save##Settings", BottomSize, BUTTON_COLOR_GREEN))
	{
		AppState->RecordHotkey       = S->TempHotkeys[0];
		AppState->CancelRecordHotkey = S->TempHotkeys[1];
		AppState->StreamHotkey       = S->TempHotkeys[2];
		AppState->LoadModelHotkey    = S->TempHotkeys[3];
		AppState->RecordHotkeyMode   = S->TempRecordHotkeyMode;

		save_hotkey_setting("record_hotkey",
			(int)AppState->RecordHotkey.Modifiers, (int)AppState->RecordHotkey.VirtualKey);
		save_hotkey_setting("cancel_record_hotkey",
			(int)AppState->CancelRecordHotkey.Modifiers, (int)AppState->CancelRecordHotkey.VirtualKey);
		save_hotkey_setting("stream_hotkey",
			(int)AppState->StreamHotkey.Modifiers, (int)AppState->StreamHotkey.VirtualKey);
		save_hotkey_setting("load_model_hotkey",
			(int)AppState->LoadModelHotkey.Modifiers, (int)AppState->LoadModelHotkey.VirtualKey);
		save_int_setting("record_hotkey_mode", (int)AppState->RecordHotkeyMode);

		AppState->PlayRecordSound = S->TempPlayRecordSound;
		save_bool_setting("play_record_sound", AppState->PlayRecordSound);

		AppState->StartSoundFreq = S->TempStartSoundFreq;
		AppState->StopSoundFreq = S->TempStopSoundFreq;
		AppState->CancelSoundFreq = S->TempCancelSoundFreq;
		save_int_setting("start_sound_freq", AppState->StartSoundFreq);
		save_int_setting("stop_sound_freq", AppState->StopSoundFreq);
		save_int_setting("cancel_sound_freq", AppState->CancelSoundFreq);

		AppState->UseCharByCharInjection = S->TempUseCharByCharInjection;
		save_bool_setting("use_char_by_char_injection", AppState->UseCharByCharInjection);

		AppState->WhisperThreadCount = S->TempWhisperThreadCount;

		ImGui::CloseCurrentPopup();
		AppState->Ui.IsSettingsDialogOpen = false;
	}
	ImGui::SameLine();
	if (colored_button("Cancel##Settings", BottomSize, BUTTON_COLOR_GREY))
	{
		ImGui::CloseCurrentPopup();
		AppState->Ui.IsSettingsDialogOpen = false;
	}

	ImGui::EndPopup();
}

static void
render_toast_ui(GlobalState *AppState, ImGuiIO &Io)
{
	if (!AppState->Ui.ToastMessage.empty() && ImGui::GetTime() < AppState->Ui.ToastExpireTime)
	{
		float Padding = 12.0f;
		ImVec2 Display = Io.DisplaySize;
		char ToastWindowName[32];
		snprintf(ToastWindowName, sizeof(ToastWindowName), "##Toast%d", AppState->Ui.ToastSerial);
		ImGui::SetNextWindowPos(
			ImVec2(Display.x * 0.5f, Display.y - 40.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.85f);
		ColorRgba Bg = AppState->Ui.ToastBackgroundColor;
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(Bg.R, Bg.G, Bg.B, Bg.A));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Padding, Padding));
		ImGui::Begin(ToastWindowName, nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
		ImGui::TextUnformatted(AppState->Ui.ToastMessage.c_str());
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);
	}
	else if (!AppState->Ui.ToastMessage.empty())
	{
		AppState->Ui.ToastMessage.clear();
	}
}

// ---------------------------------------------------------------------------
// Main Window
// ---------------------------------------------------------------------------
inline void
render_main_ui(GlobalState *AppState, ImGuiIO &Io)
{
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(Io.DisplaySize);
	ImGui::Begin(
		"VoiceTyper", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

	ImVec2 FullWidth = ImVec2(-1, 0);
	ImVec2 BigButton = ImVec2(-1, 60);
	ImVec2 SmallButton = ImVec2(-1, 40);

	bool IsModelTransitioning = AppState->IsModelTransitioning.load();
	bool Busy = AppState->IsRecording || AppState->IsStreaming ||
		AppState->PipelineActive.load() || IsModelTransitioning;

	// Record Button
	{
		ImVec4 Color = BUTTON_COLOR_GREEN;
		std::string Label = record_button_idle_label(AppState);
		bool Enabled = !AppState->IsStreaming;

		if (AppState->IsRecording)
		{
			Color = BUTTON_COLOR_RED;
			Label = "Stop (" + hotkey_to_label(AppState->RecordHotkey) + ")";
		}

		if (AppState->PipelineActive.load() && !AppState->IsRecording)
		{
			Color = BUTTON_COLOR_GREY;
			Label = "Transcribing...";
			Enabled = false;
		}
		else if (IsModelTransitioning)
		{
			Color = BUTTON_COLOR_GREY;
			Label = "Loading model...";
			Enabled = false;
		}

		if (colored_button(Label.c_str(), BigButton, Color, Enabled))
			toggle_recording(AppState);
	}

	// Cancel Record Button
	{
		bool Enabled = AppState->IsRecording;
		std::string Label = cancel_record_button_idle_label(AppState);

		if (colored_button(Label.c_str(), SmallButton, BUTTON_COLOR_GREY, Enabled))
			cancel_recording(AppState);
	}

	// Stream Button
	{
		ImVec4 Color = BUTTON_COLOR_GREEN;
		std::string Label = stream_button_idle_label(AppState);
		bool Enabled = AppState->IsStreaming ||
			(!AppState->IsRecording && !AppState->PipelineActive.load());

		if (AppState->IsStreaming)
		{
			Color = BUTTON_COLOR_RED;
			Label = "Stop Streaming (" + hotkey_to_label(AppState->StreamHotkey) + ")";
		}
		else if (IsModelTransitioning)
		{
			Color = BUTTON_COLOR_GREY;
			Label = "Loading model...";
		}

		if (colored_button(Label.c_str(), BigButton, Color, Enabled))
			toggle_streaming(AppState);
	}

	ImGui::Separator();

	// Audio Input
	{
		ImGui::Text("Audio Input");

		if (AppState->AudioInputDeviceNames.empty())
		{
			static const std::vector<std::string> NoDevices = { "No Devices Found" };
			int Dummy = 0;
			ImGui::BeginDisabled();
			ImGui::SetNextItemWidth(FullWidth.x);
			string_combo("##AudioInput", &Dummy, NoDevices);
			ImGui::EndDisabled();
		}
		else
		{
			int SelectedAudioDeviceIndex = AppState->CurrentAudioDeviceIndex;
			if (Busy) ImGui::BeginDisabled();
			ImGui::SetNextItemWidth(FullWidth.x);
			if (string_combo("##AudioInput", &SelectedAudioDeviceIndex, AppState->AudioInputDeviceNames))
				update_audio_input_selection(AppState, SelectedAudioDeviceIndex);
			if (Busy) ImGui::EndDisabled();
		}
	}

	// STT Model
	{
		ImGui::Text("STT Model");
		if (AppState->STTModelNames.empty())
		{
			std::vector<std::string> NoModels = { "No Models Found" };
			int Dummy = 0;
			ImGui::BeginDisabled();
			ImGui::SetNextItemWidth(FullWidth.x);
			string_combo("##STTModel", &Dummy, NoModels);
			ImGui::EndDisabled();
		}
		else
		{
			if (Busy) ImGui::BeginDisabled();
			ImGui::SetNextItemWidth(FullWidth.x);
			if (string_combo("##STTModel", &AppState->CurrentSTTModelIndex,
				AppState->STTModelNames))
			{
				update_stt_model_selection(AppState, AppState->CurrentSTTModelIndex);
			}
			if (Busy) ImGui::EndDisabled();
		}
	}

	// Load Model Button
	{
		bool ModelLoaded = !IsModelTransitioning && is_whisper_model_loaded(&AppState->WhisperState);
		ImVec4 Color = BUTTON_COLOR_GREY;
		std::string Label = load_model_button_idle_label(AppState);
		bool Enabled = !AppState->STTModelNames.empty() && !Busy;

		if (ModelLoaded)
		{
			Color = BUTTON_COLOR_BLUE;
			Label = "Unload STT Model (" + hotkey_to_label(AppState->LoadModelHotkey) + ")";
		}
		if (IsModelTransitioning)
		{
			Color = BUTTON_COLOR_GREY;
			Label = "Transferring model...";
		}

		if (colored_button(Label.c_str(), BigButton, Color, Enabled))
			toggle_stt_model_load(AppState);
	}

	// Inference Device
	{
		ImGui::Text("Inference Device");
		int SelectedInferenceDeviceIndex = AppState->CurrentInferenceDeviceIndex;
		if (Busy) ImGui::BeginDisabled();
		ImGui::SetNextItemWidth(FullWidth.x);
		if (string_combo("##InferenceDevice", &SelectedInferenceDeviceIndex,
			AppState->InferenceDevices))
		{
			update_inference_device_selection(AppState, SelectedInferenceDeviceIndex);
		}
		if (Busy) ImGui::EndDisabled();
	}

	ImGui::Separator();

	// Settings Button
	{
		bool Enabled = !Busy;
		if (colored_button("Settings", SmallButton, BUTTON_COLOR_GREY, Enabled))
		{
			init_settings_state(AppState);
			ImGui::OpenPopup("Settings");
		}
	}

	render_settings_ui(AppState);

	ImGui::End();

	render_toast_ui(AppState, Io);
}
