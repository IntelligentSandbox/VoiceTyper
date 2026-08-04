#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "state.h"
#include "input.h"
#include "settings.h"
#include "control.h"
#include "diagnostics.h"

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
	if (AppState->RecordHotkeyMode == RECORDING_HOTKEY_HOLD) return "Record (hold " + hotkey_to_label(AppState->RecordHotkey) + ")";

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
// Settings panel helpers (inline in right column)
// ---------------------------------------------------------------------------
static HotkeyConfig *
settings_action_hotkey_ptr(GlobalState *AppState, int Action)
{
	switch (Action)
	{
	case 0:  return &AppState->RecordHotkey;
	case 1:  return &AppState->CancelRecordHotkey;
	case 2:  return &AppState->StreamHotkey;
	case 3:  return &AppState->LoadModelHotkey;
	default: return nullptr;
	}
}

static const char *
settings_action_setting_name(int Action)
{
	switch (Action)
	{
	case 0:  return "record_hotkey";
	case 1:  return "cancel_record_hotkey";
	case 2:  return "stream_hotkey";
	case 3:  return "load_model_hotkey";
	default: return "";
	}
}

static void
settings_save_action_hotkey(GlobalState *AppState, int Action)
{
	HotkeyConfig *H = settings_action_hotkey_ptr(AppState, Action);
	if (!H) return;
	save_hotkey_setting(settings_action_setting_name(Action),
		(int)H->Modifiers, (int)H->VirtualKey);
}

static void
settings_select_action(GlobalState *AppState, int Action)
{
	SettingsWindowState *S = &AppState->Ui.SettingsState;
	S->SelectedAction = Action;

	HotkeyConfig *H = settings_action_hotkey_ptr(AppState, Action);
	S->Capture.Captured = H ? *H : HotkeyConfig{};
	S->Capture.HasCapture = S->Capture.Captured.is_valid();
	S->Capture.IsCapturing = false;
	S->Capture.PeakModifiers = 0;
	S->Capture.PeakVirtualKey = 0;
	S->Capture.ReleaseFrames = 0;
}

static void
settings_preview_sound(GlobalState *AppState, int FreqHz, bool Force)
{
	SettingsWindowState *S = &AppState->Ui.SettingsState;
	double Now = ImGui::GetTime();
	if (!Force && Now - S->LastPreviewTime < 0.15) return;
	platform_play_sound(&AppState->Platform, FreqHz, SOUND_PREVIEW_DURATION_MS);
	S->LastPreviewTime = Now;
}

// ---------------------------------------------------------------------------
// Settings panel - rendered inline in the right column
// ---------------------------------------------------------------------------
static void
render_settings_panel(GlobalState *AppState)
{
	SettingsWindowState *S = &AppState->Ui.SettingsState;

#ifdef VOICETYPER_CUDA
	ImGui::TextDisabled("v%s CUDA", VOICETYPER_VERSION_FULL);
#else
	ImGui::TextDisabled("v%s CPU", VOICETYPER_VERSION_FULL);
#endif

	if (ImGui::Checkbox("Play sound when starting/stopping/cancelling recording",
		&AppState->PlayRecordSound))
	{
		save_bool_setting("play_record_sound", AppState->PlayRecordSound);
	}

	if (AppState->PlayRecordSound)
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
			if (ImGui::SliderInt("##StartPitch", &AppState->StartSoundFreq,
				SOUND_MIN_FREQ, SOUND_MAX_FREQ, "%d Hz"))
			{
				settings_preview_sound(AppState, AppState->StartSoundFreq, false);
				save_int_setting("start_sound_freq", AppState->StartSoundFreq);
			}
			if (ImGui::IsItemDeactivated())
			{
				settings_preview_sound(AppState, AppState->StartSoundFreq, true);
				save_int_setting("start_sound_freq", AppState->StartSoundFreq);
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Stop");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderInt("##StopPitch", &AppState->StopSoundFreq,
				SOUND_MIN_FREQ, SOUND_MAX_FREQ, "%d Hz"))
			{
				settings_preview_sound(AppState, AppState->StopSoundFreq, false);
				save_int_setting("stop_sound_freq", AppState->StopSoundFreq);
			}
			if (ImGui::IsItemDeactivated())
			{
				settings_preview_sound(AppState, AppState->StopSoundFreq, true);
				save_int_setting("stop_sound_freq", AppState->StopSoundFreq);
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Cancel");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderInt("##CancelPitch", &AppState->CancelSoundFreq,
				SOUND_MIN_FREQ, SOUND_MAX_FREQ, "%d Hz"))
			{
				settings_preview_sound(AppState, AppState->CancelSoundFreq, false);
				save_int_setting("cancel_sound_freq", AppState->CancelSoundFreq);
			}
			if (ImGui::IsItemDeactivated())
			{
				settings_preview_sound(AppState, AppState->CancelSoundFreq, true);
				save_int_setting("cancel_sound_freq", AppState->CancelSoundFreq);
			}

			ImGui::EndTable();
		}

		ImGui::Unindent(20.0f);
	}

	if (ImGui::Checkbox("Use character-by-character text injection (instead of paste Ctrl+Shift+V)",
		&AppState->UseCharByCharInjection))
	{
		save_bool_setting("use_char_by_char_injection", AppState->UseCharByCharInjection);
	}

	if (ImGui::Checkbox("If no text input is focused when recording finishes, copy transcription to clipboard",
		&AppState->CopyToClipboardWhenNoTarget))
	{
		save_bool_setting("copy_to_clipboard_when_no_target", AppState->CopyToClipboardWhenNoTarget);
	}

	bool UseToggleMode = (AppState->RecordHotkeyMode == RECORDING_HOTKEY_TOGGLE);
	if (ImGui::Checkbox("Use toggle mode (press key to start/stop, instead of holding)", &UseToggleMode))
	{
		AppState->RecordHotkeyMode = UseToggleMode ? RECORDING_HOTKEY_TOGGLE : RECORDING_HOTKEY_HOLD;
		save_int_setting("record_hotkey_mode", (int)AppState->RecordHotkeyMode);
	}

	ImGui::Text("CPU Cores for Inference:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	int MaxCores = query_logical_processor_count();
	if (ImGui::InputInt("##ThreadCount", &AppState->WhisperThreadCount, 1, 1))
	{
		if (AppState->WhisperThreadCount < 1) AppState->WhisperThreadCount = 1;
		if (AppState->WhisperThreadCount > MaxCores) AppState->WhisperThreadCount = MaxCores;
	}

	ImGui::Separator();

	ImGui::SetWindowFontScale(1.3f);
	ImGui::Text("Keyboard Shortcuts");
	ImGui::SetWindowFontScale(1.0f);

	float AvailWidth = ImGui::GetContentRegionAvail().x;
	float Spacing = ImGui::GetStyle().ItemSpacing.x;
	float BtnWidth = (AvailWidth - Spacing * 3) / 4;
	ImVec2 ActionSize = ImVec2(BtnWidth, 40);

	const char *ActionLabels[] = { "Record", "Cancel Record", "Stream", "Load Model" };
	for (int i = 0; i < 4; i++)
	{
		if (i > 0) ImGui::SameLine();
		ImVec4 Color = (S->SelectedAction == i) ? BUTTON_COLOR_BLUE : BUTTON_COLOR_GREY;
		if (colored_button(ActionLabels[i], ActionSize, Color)) settings_select_action(AppState, i);
	}

	HotkeyConfig *CurrentHotkey = settings_action_hotkey_ptr(AppState, S->SelectedAction);
	if (CurrentHotkey)
	{
		ImGui::Text("Current: %s", hotkey_to_label(*CurrentHotkey).c_str());
	}

	if (S->Capture.IsCapturing)
	{
		ImGui::SetNextFrameWantCaptureKeyboard(true);
		ImGui::ClearActiveID();

		if (app_key_is_down(APP_KEY_ESCAPE))
		{
			HotkeyConfig *H = settings_action_hotkey_ptr(AppState, S->SelectedAction);
			if (H) *H = {};
			settings_save_action_hotkey(AppState, S->SelectedAction);

			S->Capture.HasCapture = false;
			S->Capture.Captured = {};
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
			}
			else if (S->Capture.PeakModifiers != 0 || S->Capture.PeakVirtualKey != 0)
			{
				S->Capture.ReleaseFrames++;
				if (S->Capture.ReleaseFrames >= 10)
				{
					S->Capture.Captured.Modifiers = S->Capture.PeakModifiers;
					S->Capture.Captured.VirtualKey = S->Capture.PeakVirtualKey;
					S->Capture.HasCapture = true;

					HotkeyConfig *H = settings_action_hotkey_ptr(AppState, S->SelectedAction);
					if (H) *H = S->Capture.Captured;
					settings_save_action_hotkey(AppState, S->SelectedAction);

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
			if (S->Capture.HasCapture) CaptureText = hotkey_to_label(S->Capture.Captured) + "...";
			else CaptureText = "Press a key combination...";
		}
		else
		{
			BgColor = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
			if (S->Capture.HasCapture) CaptureText = hotkey_to_label(S->Capture.Captured);
			else CaptureText = "Click here, then press your hotkey...";
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

	if (colored_button("Copy Exe Dir Path", ImVec2(0.0f, 0.0f), BUTTON_COLOR_GREY))
	{
		std::string ExeDir = platform_get_exe_dir();
		ImGui::SetClipboardText(ExeDir.c_str());
		show_success_toast(AppState, "Exe dir copied to clipboard!");
	}
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
// Crash Detected Dialog
// ---------------------------------------------------------------------------
static void
render_crash_dialog_ui(GlobalState *AppState)
{
	if (AppState->Ui.IsCrashDialogOpen && !AppState->Ui.CrashDialogOpened)
	{
		ImGui::OpenPopup("Crash Detected");
		AppState->Ui.CrashDialogOpened = true;
	}

	if (!AppState->Ui.IsCrashDialogOpen) return;

	ImVec2 Display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), Display);
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Crash Detected", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
	{
		return;
	}

	ImGui::TextWrapped(
		"VoiceTyper appears to have crashed on a previous run. "
		"A crash report has been saved next to the executable that the developer "
		"can use to diagnose the problem. A debug log is also saved alongside it.");

	ImGui::Separator();

	ImGui::Text("Crash report file(s):");
	ImGui::Spacing();

	for (const std::string &Path : AppState->Ui.PendingCrashDumps)
	{
		ImGui::TextWrapped("%s", Path.c_str());
	}

	ImGui::Separator();

	float AvailWidth = ImGui::GetContentRegionAvail().x;
	float Spacing = ImGui::GetStyle().ItemSpacing.x;
	float BtnWidth = (AvailWidth - Spacing) / 2;
	ImVec2 BtnSize = ImVec2(BtnWidth, 40);

	if (colored_button("Open Folder", BtnSize, BUTTON_COLOR_GREY))
	{
		if (!AppState->Ui.PendingCrashDumps.empty())
		{
			platform_open_folder_selecting_file(AppState->Ui.PendingCrashDumps[0]);
		}
	}

	ImGui::SameLine();

	if (colored_button("Dismiss##CrashDialog", BtnSize, BUTTON_COLOR_GREY))
	{
		for (const std::string &Path : AppState->Ui.PendingCrashDumps)
		{
			mark_crash_dump_seen(Path);
		}

		AppState->Ui.PendingCrashDumps.clear();
		AppState->Ui.IsCrashDialogOpen = false;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Left column - recording controls and model selectors
// ---------------------------------------------------------------------------
static void
render_left_panel(GlobalState *AppState)
{
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

		if (colored_button(Label.c_str(), BigButton, Color, Enabled)) toggle_recording(AppState);
	}

	// Cancel Record Button
	{
		bool Enabled = AppState->IsRecording;
		std::string Label = cancel_record_button_idle_label(AppState);

		if (colored_button(Label.c_str(), SmallButton, BUTTON_COLOR_GREY, Enabled)) cancel_recording(AppState);
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

		if (colored_button(Label.c_str(), BigButton, Color, Enabled)) toggle_streaming(AppState);
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
			if (string_combo("##AudioInput", &SelectedAudioDeviceIndex, AppState->AudioInputDeviceNames)) update_audio_input_selection(AppState, SelectedAudioDeviceIndex);
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

		if (colored_button(Label.c_str(), BigButton, Color, Enabled)) toggle_stt_model_load(AppState);
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
}

// ---------------------------------------------------------------------------
// Bottom bar - live operation timings
// ---------------------------------------------------------------------------
static std::string
format_timing_ms(double Ms)
{
	if (Ms < 0.0) return "\xe2\x80\x94";

	char Buf[64];
	if (Ms < 1000.0) snprintf(Buf, sizeof(Buf), "%.4f ms", Ms);
	else snprintf(Buf, sizeof(Buf), "%.4f s", Ms / 1000.0);
	return std::string(Buf);
}

static void
render_bottom_bar(GlobalState *AppState)
{
	ImGui::Separator();

	ImGui::TextDisabled("Timings");
	ImGui::SameLine();

	ImGui::Text("Model load: %s", format_timing_ms(AppState->LastModelLoadMs.load()).c_str());
	ImGui::SameLine();
	ImGui::Text("Transcription: %s", format_timing_ms(AppState->LastTranscriptionMs.load()).c_str());
	ImGui::SameLine();
	ImGui::Text("Paste: %s", format_timing_ms(AppState->LastPasteMs.load()).c_str());
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

	const float Padding = 16.0f;
	const float ColumnWidth = (Io.DisplaySize.x - Padding * 3.0f) * 0.5f;

	ImGui::SetCursorPos(ImVec2(Padding, Padding));
	ImGui::BeginChild("##LeftColumn", ImVec2(ColumnWidth, 0.0f),
		ImGuiChildFlags_AutoResizeY,
		ImGuiWindowFlags_NoScrollbar);
	render_left_panel(AppState);
	ImGui::EndChild();
	const float LeftEndY = ImGui::GetCursorPosY();

	ImGui::SetCursorPos(ImVec2(Padding * 2.0f + ColumnWidth, Padding));
	ImGui::BeginChild("##RightColumn", ImVec2(ColumnWidth, 0.0f),
		ImGuiChildFlags_AutoResizeY,
		ImGuiWindowFlags_NoScrollbar);
	render_settings_panel(AppState);
	ImGui::EndChild();
	const float RightEndY = ImGui::GetCursorPosY();

	float TallerEndY = (LeftEndY > RightEndY) ? LeftEndY : RightEndY;
	ImGui::SetCursorPos(ImVec2(Padding, TallerEndY + Padding));
	render_bottom_bar(AppState);

	render_crash_dialog_ui(AppState);

	ImGui::End();

	render_toast_ui(AppState, Io);
}
