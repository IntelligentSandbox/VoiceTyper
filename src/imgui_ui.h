#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "state.h"
#include "settings.h"
#include "control.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Styled button helper
// ---------------------------------------------------------------------------
static
bool
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
static
bool
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

static
void
format_int_text(char *Buffer, size_t BufferSize, int Value)
{
	snprintf(Buffer, BufferSize, "%d", Value);
}

static
bool
parse_bounded_int_text(const char *Text, int Min, int Max, int *OutValue)
{
	if (!Text || Text[0] == '\0')
		return false;

	errno = 0;
	char *End = nullptr;
	long Value = strtol(Text, &End, 10);
	if (errno != 0 || End == Text || *End != '\0')
		return false;

	if (Value < Min || Value > Max)
		return false;

	*OutValue = (int)Value;
	return true;
}

static
bool
validate_bounded_int_text(GlobalState *AppState, const char *FieldName,
	const char *Text, int Min, int Max, int *OutValue)
{
	if (parse_bounded_int_text(Text, Min, Max, OutValue))
		return true;

	char Message[128];
	snprintf(Message, sizeof(Message), "%s must be a number between %d and %d.",
		FieldName, Min, Max);
	show_toast(AppState, Message);
	return false;
}

static
void
render_numeric_text_input(const char *Id, char *Buffer, size_t BufferSize,
	int Min, int Max, int WheelStep)
{
	ImGui::InputText(Id, Buffer, BufferSize, ImGuiInputTextFlags_CharsDecimal);
	if (!ImGui::IsItemActive())
		return;

	float Wheel = ImGui::GetIO().MouseWheel;
	if (Wheel == 0.0f)
		return;

	int Value = 0;
	if (!parse_bounded_int_text(Buffer, Min, Max, &Value))
		return;

	Value += (Wheel > 0.0f) ? WheelStep : -WheelStep;
	if (Value < Min) Value = Min;
	if (Value > Max) Value = Max;
	format_int_text(Buffer, BufferSize, Value);
}

// ---------------------------------------------------------------------------
// Hotkey capture helpers
// ---------------------------------------------------------------------------
static
UINT
poll_modifier_state()
{
	UINT Mods = 0;
	if (platform_is_key_down(VK_CONTROL)) Mods |= HOTKEY_MOD_CTRL;
	if (platform_is_key_down(VK_MENU))    Mods |= HOTKEY_MOD_ALT;
	if (platform_is_key_down(VK_SHIFT))   Mods |= HOTKEY_MOD_SHIFT;
	if (platform_is_key_down(VK_LWIN))    Mods |= HOTKEY_MOD_WIN;
	return Mods;
}

static
UINT
poll_nonmodifier_vk()
{
	for (UINT Vk = 'A'; Vk <= 'Z'; Vk++)
	{
		if (platform_is_key_down(Vk)) return Vk;
	}
	for (UINT Vk = '0'; Vk <= '9'; Vk++)
	{
		if (platform_is_key_down(Vk)) return Vk;
	}
	for (UINT Vk = VK_F1; Vk <= VK_F24; Vk++)
	{
		if (platform_is_key_down(Vk)) return Vk;
	}
	UINT Specials[] = {
		VK_SPACE, VK_RETURN, VK_TAB, VK_BACK, VK_DELETE, VK_INSERT,
		VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
		VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN
	};
	for (int i = 0; i < (int)(sizeof(Specials) / sizeof(Specials[0])); i++)
	{
		if (platform_is_key_down(Specials[i])) return Specials[i];
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Settings Dialog - select action
// ---------------------------------------------------------------------------
static
void
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
static
void
init_settings_state(GlobalState *AppState)
{
	SettingsWindowState *S = &AppState->SettingsState;
	S->SelectedAction = 0;
	S->TempHotkeys[0] = AppState->RecordHotkey;
	S->TempHotkeys[1] = AppState->CancelRecordHotkey;
	S->TempHotkeys[2] = AppState->StreamHotkey;
	S->TempHotkeys[3] = AppState->LoadModelHotkey;
	S->TempPlayRecordSound = AppState->PlayRecordSound;
	S->TempStartSound = AppState->StartSound;
	S->TempStopSound = AppState->StopSound;
	S->TempCancelSound = AppState->CancelSound;
	S->TempUseCharByCharInjection = AppState->UseCharByCharInjection;
	S->TempWhisperThreadCount = AppState->WhisperThreadCount;
	format_int_text(S->TempStartSoundFreqText, sizeof(S->TempStartSoundFreqText),
		S->TempStartSound.FreqHz);
	format_int_text(S->TempStartSoundVolumeText, sizeof(S->TempStartSoundVolumeText),
		S->TempStartSound.Volume);
	format_int_text(S->TempStopSoundFreqText, sizeof(S->TempStopSoundFreqText),
		S->TempStopSound.FreqHz);
	format_int_text(S->TempStopSoundVolumeText, sizeof(S->TempStopSoundVolumeText),
		S->TempStopSound.Volume);
	format_int_text(S->TempCancelSoundFreqText, sizeof(S->TempCancelSoundFreqText),
		S->TempCancelSound.FreqHz);
	format_int_text(S->TempCancelSoundVolumeText,
		sizeof(S->TempCancelSoundVolumeText), S->TempCancelSound.Volume);
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
static
void
render_settings_ui(GlobalState *AppState)
{
	SettingsWindowState *S = &AppState->SettingsState;

	ImVec2 Display = ImGui::GetIO().DisplaySize;
	float SettingsW = (Display.x < 620.0f) ? Display.x : 620.0f;
	ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), Display);
	ImGui::SetNextWindowSize(ImVec2(SettingsW, 0));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
		return;

	AppState->IsSettingsDialogOpen = true;

#ifdef VOICETYPER_CUDA
	ImGui::TextDisabled("v%s CUDA", VOICETYPER_VERSION);
#else
	ImGui::TextDisabled("v%s CPU", VOICETYPER_VERSION);
#endif

	ImGui::Checkbox("Play sound when starting/stopping/cancelling recording",
		&S->TempPlayRecordSound);

	if (S->TempPlayRecordSound)
	{
		ImGui::Indent(20.0f);

		if (ImGui::BeginTable("##SoundSettings", 3,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableSetupColumn("Sound");
			ImGui::TableSetupColumn("Pitch\n(200-2000 Hz)");
			ImGui::TableSetupColumn("Volume\n(1-100%)");
			ImGui::TableHeadersRow();

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Start");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			render_numeric_text_input("##StartPitch", S->TempStartSoundFreqText,
				sizeof(S->TempStartSoundFreqText), SOUND_MIN_FREQ,
				SOUND_MAX_FREQ, 10);
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1.0f);
			render_numeric_text_input("##StartVolume", S->TempStartSoundVolumeText,
				sizeof(S->TempStartSoundVolumeText), 1, 100, 1);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Stop");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			render_numeric_text_input("##StopPitch", S->TempStopSoundFreqText,
				sizeof(S->TempStopSoundFreqText), SOUND_MIN_FREQ,
				SOUND_MAX_FREQ, 10);
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1.0f);
			render_numeric_text_input("##StopVolume", S->TempStopSoundVolumeText,
				sizeof(S->TempStopSoundVolumeText), 1, 100, 1);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Cancel");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			render_numeric_text_input("##CancelPitch", S->TempCancelSoundFreqText,
				sizeof(S->TempCancelSoundFreqText), SOUND_MIN_FREQ,
				SOUND_MAX_FREQ, 10);
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1.0f);
			render_numeric_text_input("##CancelVolume",
				S->TempCancelSoundVolumeText,
				sizeof(S->TempCancelSoundVolumeText), 1, 100, 1);

			ImGui::EndTable();
		}

		ImGui::Unindent(20.0f);
	}

	ImGui::Checkbox("Use character-by-character text injection (instead of paste)",
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

	ImGui::Separator();
	ImGui::Text("Keyboard Shortcuts");

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

	ImGui::Text("Current: %s", S->TempHotkeys[S->SelectedAction].to_label().c_str());

	if (S->Capture.IsCapturing)
	{
		ImGui::SetNextFrameWantCaptureKeyboard(true);
		ImGui::ClearActiveID();

		if (platform_is_key_down(VK_ESCAPE))
		{
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
			UINT Mods = poll_modifier_state();
			UINT Vk = poll_nonmodifier_vk();

			if (Mods != 0 || Vk != 0)
			{
				S->Capture.PeakModifiers |= Mods;
				if (Vk != 0) S->Capture.PeakVirtualKey = Vk;
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
				CaptureText = S->Capture.Captured.to_label() + "...";
			else
				CaptureText = "Press a key combination...";
		}
		else
		{
			BgColor = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
			if (S->Capture.HasCapture)
				CaptureText = S->Capture.Captured.to_label();
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
		"Modifier-only combos (e.g. Ctrl+Alt) are supported. Escape clears the box.");

	ImGui::Separator();

	float BottomBtnWidth = (AvailWidth - Spacing * 2) / 3;
	ImVec2 BottomSize = ImVec2(BottomBtnWidth, 40);

	if (colored_button("Set Hotkey", BottomSize, BUTTON_COLOR_GREY))
	{
		if (S->Capture.HasCapture)
		{
			S->TempHotkeys[S->SelectedAction] = S->Capture.Captured;
		}
	}
	ImGui::SameLine();
	if (colored_button("Save##Settings", BottomSize, BUTTON_COLOR_GREEN))
	{
		SoundConfig StartSound = AppState->StartSound;
		SoundConfig StopSound = AppState->StopSound;
		SoundConfig CancelSound = AppState->CancelSound;

		if (S->TempPlayRecordSound)
		{
			if (!validate_bounded_int_text(AppState, "Start sound pitch",
				S->TempStartSoundFreqText, SOUND_MIN_FREQ, SOUND_MAX_FREQ,
				&StartSound.FreqHz))
			{
				return;
			}
			if (!validate_bounded_int_text(AppState, "Start sound volume",
				S->TempStartSoundVolumeText, 1, 100, &StartSound.Volume))
			{
				return;
			}
			if (!validate_bounded_int_text(AppState, "Stop sound pitch",
				S->TempStopSoundFreqText, SOUND_MIN_FREQ, SOUND_MAX_FREQ,
				&StopSound.FreqHz))
			{
				return;
			}
			if (!validate_bounded_int_text(AppState, "Stop sound volume",
				S->TempStopSoundVolumeText, 1, 100, &StopSound.Volume))
			{
				return;
			}
			if (!validate_bounded_int_text(AppState, "Cancel sound pitch",
				S->TempCancelSoundFreqText, SOUND_MIN_FREQ, SOUND_MAX_FREQ,
				&CancelSound.FreqHz))
			{
				return;
			}
			if (!validate_bounded_int_text(AppState, "Cancel sound volume",
				S->TempCancelSoundVolumeText, 1, 100, &CancelSound.Volume))
			{
				return;
			}
		}

		AppState->RecordHotkey       = S->TempHotkeys[0];
		AppState->CancelRecordHotkey = S->TempHotkeys[1];
		AppState->StreamHotkey       = S->TempHotkeys[2];
		AppState->LoadModelHotkey    = S->TempHotkeys[3];

		save_hotkey_setting("record_hotkey",
			(int)AppState->RecordHotkey.Modifiers, (int)AppState->RecordHotkey.VirtualKey);
		save_hotkey_setting("cancel_record_hotkey",
			(int)AppState->CancelRecordHotkey.Modifiers, (int)AppState->CancelRecordHotkey.VirtualKey);
		save_hotkey_setting("stream_hotkey",
			(int)AppState->StreamHotkey.Modifiers, (int)AppState->StreamHotkey.VirtualKey);
		save_hotkey_setting("load_model_hotkey",
			(int)AppState->LoadModelHotkey.Modifiers, (int)AppState->LoadModelHotkey.VirtualKey);

		AppState->PlayRecordSound = S->TempPlayRecordSound;
		save_bool_setting("play_record_sound", AppState->PlayRecordSound);

		S->TempStartSound = StartSound;
		S->TempStopSound = StopSound;
		S->TempCancelSound = CancelSound;
		AppState->StartSound = S->TempStartSound;
		AppState->StopSound = S->TempStopSound;
		AppState->CancelSound = S->TempCancelSound;
		save_int_setting("start_sound_freq", AppState->StartSound.FreqHz);
		save_int_setting("start_sound_volume", AppState->StartSound.Volume);
		save_int_setting("stop_sound_freq", AppState->StopSound.FreqHz);
		save_int_setting("stop_sound_volume", AppState->StopSound.Volume);
		save_int_setting("cancel_sound_freq", AppState->CancelSound.FreqHz);
		save_int_setting("cancel_sound_volume", AppState->CancelSound.Volume);

		AppState->UseCharByCharInjection = S->TempUseCharByCharInjection;
		save_bool_setting("use_char_by_char_injection", AppState->UseCharByCharInjection);

		AppState->WhisperThreadCount = S->TempWhisperThreadCount;

		ImGui::CloseCurrentPopup();
		AppState->IsSettingsDialogOpen = false;
	}
	ImGui::SameLine();
	if (colored_button("Cancel##Settings", BottomSize, BUTTON_COLOR_GREY))
	{
		ImGui::CloseCurrentPopup();
		AppState->IsSettingsDialogOpen = false;
	}

	ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Main Window
// ---------------------------------------------------------------------------
inline
void
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
			Label = "Stop (" + AppState->RecordHotkey.to_label() + ")";
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
			Label = "Stop Streaming (" + AppState->StreamHotkey.to_label() + ")";
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
		std::vector<std::string> DeviceNames;
		for (const auto &Dev : AppState->AudioInputDevices)
			DeviceNames.push_back(Dev.Name);

		if (DeviceNames.empty())
		{
			DeviceNames.push_back("No Devices Found");
			int Dummy = 0;
			ImGui::BeginDisabled();
			ImGui::SetNextItemWidth(FullWidth.x);
			string_combo("##AudioInput", &Dummy, DeviceNames);
			ImGui::EndDisabled();
		}
		else
		{
			int SelectedAudioDeviceIndex = AppState->CurrentAudioDeviceIndex;
			if (Busy) ImGui::BeginDisabled();
			ImGui::SetNextItemWidth(FullWidth.x);
			if (string_combo("##AudioInput", &SelectedAudioDeviceIndex, DeviceNames))
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
			Label = "Unload STT Model (" + AppState->LoadModelHotkey.to_label() + ")";
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

	if (!AppState->ToastMessage.empty() && ImGui::GetTime() < AppState->ToastExpireTime)
	{
		float Padding = 12.0f;
		ImVec2 Display = Io.DisplaySize;
		ImGui::SetNextWindowPos(
			ImVec2(Display.x * 0.5f, Display.y - 40.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.85f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Padding, Padding));
		ImGui::Begin("##Toast", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
		ImGui::TextUnformatted(AppState->ToastMessage.c_str());
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);
	}
	else if (!AppState->ToastMessage.empty())
	{
		AppState->ToastMessage.clear();
	}

	ImGui::End();
}
