#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "state.h"
#include "input.h"
#include "settings.h"
#include "control.h"
#include "diagnostics.h"
#include "model_catalog.h"
#include "model_assets.h"
#include "model_downloader.h"
#include "updater.h"

#include <cstdio>

#define BUTTON_COLOR_GREEN   ImVec4(0.0f, 0.50f, 0.0f, 1.0f)
#define BUTTON_COLOR_RED     ImVec4(0.75f, 0.07f, 0.13f, 1.0f)
#define BUTTON_COLOR_GREY    ImVec4(0.50f, 0.50f, 0.50f, 1.0f)
#define BUTTON_COLOR_BLUE    ImVec4(0.13f, 0.59f, 0.95f, 1.0f)

#define FONT_SUGGESTION_MAX_ROWS 5

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
// Update modal - floating window opened from the settings panel
// ---------------------------------------------------------------------------
static void
render_update_modal(GlobalState *AppState)
{
	UpdateState *U = &AppState->Ui.Update;

	poll_update_check(AppState);
	poll_update_download(AppState);

	if (U->DownloadJustFinished)
	{
		U->DownloadJustFinished = false;

		if (U->DownloadSucceeded.load() && U->ApplyOnDownload)
		{
			if (updater_apply_downloaded_update(AppState))
			{
				AppState->ExitRequested.store(true);
				return;
			}
			show_toast(AppState, "Update downloaded but the update step failed to launch.");
		}
	}

	if (!U->IsModalOpen) return;

	ImVec2 Display = ImGui::GetIO().DisplaySize;
	float WinW = Display.x * 0.6f;
	if (WinW > 560.0f) WinW = 560.0f;
	ImGui::SetNextWindowSize(ImVec2(WinW, 0.0f), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool Open = true;
	if (ImGui::Begin("Check for Updates", &Open,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
	{
		ImGui::TextDisabled("v%s", VOICETYPER_VERSION_FULL);
		ImGui::Spacing();

		if (U->DownloadRunning.load())
		{
			int64_t Done = U->DownloadedBytes.load();
			int64_t Total = U->TotalBytes.load();
			float Fraction = Total > 0 ? (float)((double)Done / (double)Total) : 0.0f;
			char Overlay[64];
			snprintf(Overlay, sizeof(Overlay), "%.1f / %.1f MB",
				(double)Done / 1000000.0, (double)Total / 1000000.0);
			ImGui::ProgressBar(Fraction, ImVec2(-1.0f, 0.0f), Overlay);
			if (ImGui::Button("Cancel update download"))
			{
				cancel_update_download(AppState);
			}
		}
		else if (U->CheckRunning.load())
		{
			ImGui::TextDisabled("Checking for updates...");
		}
		else
		{
			const char *CheckLabel = (U->CheckSucceeded.load() || U->CheckFailed.load()) ?
				"Check again" : "Check for updates";
			if (ImGui::Button(CheckLabel))
			{
				start_update_check(AppState);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Releases page"))
			{
				platform_open_url(U->ReleaseUrl.empty() ? UPDATER_RELEASES_URL : U->ReleaseUrl.c_str());
			}

			if (U->CheckFailed.load())
			{
				ImGui::Text("Update check failed (GitHub unreachable?)");
			}
			else if (U->CheckSucceeded.load())
			{
				if (U->Assets.empty())
				{
					ImGui::TextDisabled("No matching downloads for this OS.");
				}
				else
				{
					if (U->IsNewerAvailable)
					{
						ImGui::TextColored(ImVec4(0.20f, 0.90f, 0.30f, 1.0f), "Update available: %s",
							U->LatestVersion.c_str());
					}
					else
					{
						ImGui::TextDisabled("Up to date (%s)", U->LatestVersion.c_str());
					}

					for (const UpdateAssetInfo &Asset : U->Assets)
					{
						ImGui::PushID(Asset.Name.c_str());
						ImGui::BulletText("%s (%.1f MB)", Asset.Name.c_str(), (double)Asset.Size / 1000000.0);

#ifdef _WIN32
						bool IsMsi = updater_string_ends_with(Asset.Name, ".msi");
						const char *ActionLabel = IsMsi ? "Run installer" : "Update portable";
#else
						const char *ActionLabel = "Update portable";
#endif

						ImGui::SameLine();
						if (ImGui::SmallButton(ActionLabel))
						{
							start_update_download(AppState, Asset, true);
						}
						ImGui::PopID();
					}
				}
			}
		}

		ImGui::TextDisabled("%s", platform_is_installed_build() ?
			"Installed (MSI) build detected" : "Portable build detected");

		ImGui::Separator();
		if (ImGui::Button("Close")) U->IsModalOpen = false;
	}
	ImGui::End();

	if (!Open) U->IsModalOpen = false;
}

// ---------------------------------------------------------------------------
// Font name input with a live typeahead dropdown of matching system fonts
// ---------------------------------------------------------------------------
static void
font_name_apply(GlobalState *AppState, const char *Name)
{
	AppState->UiFontName = Name;
	save_string_setting("ui_font_name", AppState->UiFontName.c_str());
	AppState->Ui.FontReloadRequested = true;
}

struct FontNameInputNav
{
	SettingsWindowState *S;
	bool ScrollToSelection;
};

static int
font_name_input_callback(ImGuiInputTextCallbackData *Data)
{
	FontNameInputNav *Nav = (FontNameInputNav *)Data->UserData;
	SettingsWindowState *S = Nav->S;
	int Count = S->FontSuggestionMatchCount;
	if (Count <= 0)
	{
		return 0;
	}

	if (Data->EventKey == ImGuiKey_UpArrow)
	{
		if (S->FontSuggestionIndex < 0)
		{
			S->FontSuggestionIndex = Count - 1;
		}
		else if (S->FontSuggestionIndex == 0)
		{
			S->FontSuggestionIndex = -1;
		}
		else
		{
			S->FontSuggestionIndex--;
		}
		Nav->ScrollToSelection = true;
	}
	else if (Data->EventKey == ImGuiKey_DownArrow)
	{
		if (S->FontSuggestionIndex < 0)
		{
			S->FontSuggestionIndex = 0;
		}
		else if (S->FontSuggestionIndex >= Count - 1)
		{
			S->FontSuggestionIndex = -1;
		}
		else
		{
			S->FontSuggestionIndex++;
		}
		Nav->ScrollToSelection = true;
	}

	return 0;
}

static void
render_font_name_input(GlobalState *AppState)
{
	SettingsWindowState *S = &AppState->Ui.SettingsState;

	ImGui::TextUnformatted("Font");
	ImGui::SetNextItemWidth(-1.0f);

	// CallbackHistory makes the InputText claim the Up/Down arrow keys so
	// keyboard nav doesn't move focus to other widgets while the suggestion
	// dropdown is open.
	FontNameInputNav Nav = {};
	Nav.S = S;
	ImGuiInputTextFlags InputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
		ImGuiInputTextFlags_CallbackHistory;
	bool EnterPressed = ImGui::InputTextWithHint("##UiFontName", "Type to search system fonts...",
		S->FontNameBuffer, sizeof(S->FontNameBuffer), InputFlags, font_name_input_callback, &Nav);
	// Enter deactivates the input, so capture the highlighted suggestion before
	// the loss-of-focus reset below wipes it.
	int EnterSelection = S->FontSuggestionIndex;
	if (ImGui::IsItemEdited())
	{
		S->FontSuggestionIndex = -1;
	}

	bool InputActive = ImGui::IsItemActive();
	if (!InputActive && S->FontSuggestionIndex != -1)
	{
		S->FontSuggestionIndex = -1;
	}

	std::string LowerNeedle;
	for (int i = 0; S->FontNameBuffer[i] != '\0'; i++)
	{
		LowerNeedle += ascii_lower(S->FontNameBuffer[i]);
	}

	std::vector<std::string> Matches;
	for (const std::string &Name : AppState->UiFontNames)
	{
		if (ascii_contains_ci(Name, LowerNeedle))
		{
			Matches.push_back(Name);
		}
	}

	if (S->FontSuggestionIndex >= (int)Matches.size())
	{
		S->FontSuggestionIndex = (int)Matches.size() - 1;
	}
	S->FontSuggestionMatchCount = (int)Matches.size();

	if (EnterPressed)
	{
		if (EnterSelection >= 0 && EnterSelection < (int)Matches.size())
		{
			snprintf(S->FontNameBuffer, sizeof(S->FontNameBuffer), "%s", Matches[EnterSelection].c_str());
		}
		font_name_apply(AppState, S->FontNameBuffer);
		S->FontSuggestionIndex = -1;
	}
	else if (ImGui::IsItemDeactivatedAfterEdit())
	{
		// A click on a suggestion row deactivates the input mid-click; don't
		// commit the typed text then (the row click applies instead, and the
		// mid-click font reload would flash a load-failure toast).
		ImGuiStyle &Style = ImGui::GetStyle();
		ImVec2 ItemMin = ImGui::GetItemRectMin();
		ImVec2 ItemMax = ImGui::GetItemRectMax();
		float Width = ImGui::GetItemRectSize().x;
		float RowHeight = ImGui::GetFrameHeight() + Style.ItemSpacing.y;
		int VisibleRows = (int)Matches.size();
		if (VisibleRows > FONT_SUGGESTION_MAX_ROWS) VisibleRows = FONT_SUGGESTION_MAX_ROWS;
		float Height = Style.WindowPadding.y * 2.0f +
			RowHeight * (float)VisibleRows - Style.ItemSpacing.y;
		ImVec2 PopupMin = ImVec2(ItemMin.x, ItemMax.y + Style.FramePadding.y);
		ImVec2 PopupMax = ImVec2(PopupMin.x + Width, PopupMin.y + Height);
		if (!ImGui::IsMouseHoveringRect(PopupMin, PopupMax, false))
		{
			font_name_apply(AppState, S->FontNameBuffer);
		}
	}

	if ((InputActive || ImGui::IsPopupOpen("##FontSuggestions")) && !Matches.empty())
	{
		ImGui::OpenPopup("##FontSuggestions");

		ImGuiStyle &Style = ImGui::GetStyle();
		ImVec2 ItemMin = ImGui::GetItemRectMin();
		ImVec2 ItemMax = ImGui::GetItemRectMax();
		float Width = ImGui::GetItemRectSize().x;
		float RowHeight = ImGui::GetFrameHeight() + Style.ItemSpacing.y;
		int VisibleRows = (int)Matches.size();
		if (VisibleRows > FONT_SUGGESTION_MAX_ROWS) VisibleRows = FONT_SUGGESTION_MAX_ROWS;
		float Height = Style.WindowPadding.y * 2.0f +
			RowHeight * (float)VisibleRows - Style.ItemSpacing.y;

		ImGui::SetNextWindowPos(ImVec2(ItemMin.x, ItemMax.y + Style.FramePadding.y));
		ImGui::SetNextWindowSize(ImVec2(Width, Height));
	}

	if (ImGui::BeginPopup("##FontSuggestions", ImGuiWindowFlags_NoFocusOnAppearing))
	{
		// Clicking into the input focuses the main window and brings it to the
		// display front, which would leave this reused popup window rendering
		// behind it (NoFocusOnAppearing skips the popup's own bring-to-front).
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		// While the click is held the input's active-id blocks plain hover
		// queries, which would close the popup on the mouse-down frame and
		// swallow the click (same reason combos use this flag).
		bool KeepOpen = InputActive ||
			ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		if (!KeepOpen || Matches.empty())
		{
			ImGui::CloseCurrentPopup();
		}
		else
		{
			int RowIndex = 0;
			for (const std::string &Name : Matches)
			{
				bool IsCurrent = ascii_equals_ci(Name, AppState->UiFontName);
				bool IsSelected = RowIndex == S->FontSuggestionIndex;
				if (ImGui::Selectable(Name.c_str(), IsCurrent || IsSelected))
				{
					snprintf(S->FontNameBuffer, sizeof(S->FontNameBuffer), "%s", Name.c_str());
					font_name_apply(AppState, Name.c_str());
					ImGui::CloseCurrentPopup();
					S->FontSuggestionIndex = -1;
				}
				if (IsSelected && Nav.ScrollToSelection)
				{
					ImGui::SetScrollHereY();
				}
				RowIndex++;
			}
		}
		ImGui::EndPopup();
	}
}

// ---------------------------------------------------------------------------
// Settings panel - rendered inline in the right column
// ---------------------------------------------------------------------------
static void
render_settings_panel(GlobalState *AppState)
{
	SettingsWindowState *S = &AppState->Ui.SettingsState;

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

	if (ImGui::Checkbox("Show word confidence colors in the Transcribed Text box",
		&AppState->ShowTranscribedTextConfidence))
	{
		save_bool_setting("show_transcribed_text_confidence", AppState->ShowTranscribedTextConfidence);
	}

	bool UseToggleMode = (AppState->RecordHotkeyMode == RECORDING_HOTKEY_TOGGLE);
	if (ImGui::Checkbox("Use toggle mode (press key to start/stop, instead of holding)", &UseToggleMode))
	{
		AppState->RecordHotkeyMode = UseToggleMode ? RECORDING_HOTKEY_TOGGLE : RECORDING_HOTKEY_HOLD;
		save_int_setting("record_hotkey_mode", (int)AppState->RecordHotkeyMode);
	}

	ImGui::Separator();
	render_font_name_input(AppState);

	ImGui::TextUnformatted("Font size");
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::SliderInt("##UiFontSize", &AppState->UiFontSize, 8, 72, "%d px"))
	{
		save_int_setting("ui_font_size", AppState->UiFontSize);
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

	float UtilityBtnWidth = (AvailWidth - Spacing) / 2;
	ImVec2 UtilityBtnSize = ImVec2(UtilityBtnWidth, 0.0f);

	if (colored_button("Copy Exe Dir Path", UtilityBtnSize, BUTTON_COLOR_GREY))
	{
		std::string ExeDir = platform_get_exe_dir();
		ImGui::SetClipboardText(ExeDir.c_str());
		show_success_toast(AppState, "Exe dir copied to clipboard!");
	}

	ImGui::SameLine();

	if (colored_button("Check for Updates", UtilityBtnSize, BUTTON_COLOR_GREY))
	{
		AppState->Ui.Update.IsModalOpen = true;
		start_update_check(AppState);
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
		"can use to diagnose the problem.");

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

		if (colored_button("Download Models...", SmallButton, BUTTON_COLOR_GREY))
		{
			AppState->Ui.Download.IsModalOpen = true;
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
		bool DevicesLoaded = AppState->InferenceDevicesLoaded.load(std::memory_order_acquire);
		if (!DevicesLoaded)
		{
			refresh_inference_devices(AppState);
		}

		ImGui::Text("Inference Device");
		int SelectedInferenceDeviceIndex = AppState->CurrentInferenceDeviceIndex;
		if (Busy) ImGui::BeginDisabled();
		ImGui::SetNextItemWidth(FullWidth.x);
		if (DevicesLoaded)
		{
			if (string_combo("##InferenceDevice", &SelectedInferenceDeviceIndex,
				AppState->InferenceDevices))
			{
				update_inference_device_selection(AppState, SelectedInferenceDeviceIndex);
			}
		}
		else
		{
			static const char *LoadingItems[] = {"CPU"};
			int LoadingIdx = 0;
			ImGui::Combo("##InferenceDevice", &LoadingIdx, LoadingItems, 1);
		}
		if (Busy) ImGui::EndDisabled();
		if (!DevicesLoaded)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(loading GPU devices...)");
		}
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

static std::string
format_bytes(int64_t Bytes)
{
	char Buf[32];
	if (Bytes >= 1073741824) snprintf(Buf, sizeof(Buf), "%.2f GB", Bytes / 1073741824.0);
	else if (Bytes >= 1048576) snprintf(Buf, sizeof(Buf), "%d MB", (int)(Bytes / 1048576));
	else if (Bytes >= 1024) snprintf(Buf, sizeof(Buf), "%d KB", (int)(Bytes / 1024));
	else snprintf(Buf, sizeof(Buf), "%d B", (int)Bytes);
	return std::string(Buf);
}

static bool
model_filename_installed(GlobalState *AppState, const std::string &Filename)
{
	for (const std::string &Path : AppState->STTModelPaths)
	{
		size_t Slash = Path.find_last_of("\\/");
		std::string Fname = (Slash == std::string::npos) ? Path : Path.substr(Slash + 1);
		if (Fname == Filename) return true;
	}
	return false;
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

static ImVec4
transcribed_word_confidence_color(float Confidence)
{
	if (Confidence >= 0.85f) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	if (Confidence >= 0.60f) return ImVec4(0.92f, 0.78f, 0.30f, 1.0f);
	return ImVec4(0.92f, 0.35f, 0.35f, 1.0f);
}

static void
render_transcribed_text_box(GlobalState *AppState)
{
	UiRuntimeState *Ui = &AppState->Ui;

	{
		std::lock_guard<std::mutex> Lock(Ui->TranscribedTextMutex);
		if (Ui->TranscribedTextSerial != Ui->TranscribedTextBoxSerial)
		{
			Ui->TranscribedTextBoxSerial = Ui->TranscribedTextSerial;
			Ui->TranscribedTextBoxWords = Ui->TranscribedTextWords;

			Ui->TranscribedTextBoxBuffer.clear();
			for (const TranscribedWord &Word : Ui->TranscribedTextBoxWords)
			{
				Ui->TranscribedTextBoxBuffer.insert(
					Ui->TranscribedTextBoxBuffer.end(),
					Word.Text.begin(), Word.Text.end());
			}
			Ui->TranscribedTextBoxBuffer.push_back('\0');
		}
	}

	if (Ui->TranscribedTextBoxBuffer.empty()) Ui->TranscribedTextBoxBuffer.push_back('\0');

	ImGui::TextDisabled("Transcribed Text");

	const float BoxHeight = ImGui::GetTextLineHeightWithSpacing() * 6.0f +
		ImGui::GetStyle().FramePadding.y * 2.0f;

	if (!AppState->ShowTranscribedTextConfidence)
	{
		ImGui::InputTextMultiline("##TranscribedText",
			Ui->TranscribedTextBoxBuffer.data(),
			Ui->TranscribedTextBoxBuffer.size(),
			ImVec2(-1.0f, BoxHeight),
			ImGuiInputTextFlags_ReadOnly);
		return;
	}

	if (ImGui::BeginChild("##TranscribedTextConfidence", ImVec2(-1.0f, BoxHeight), ImGuiChildFlags_Borders))
	{
		for (const TranscribedWord &Word : Ui->TranscribedTextBoxWords)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, transcribed_word_confidence_color(Word.Confidence));
			ImGui::TextUnformatted(Word.Text.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(0.0f, 0.0f);
		}
	}
	ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Model downloader modal
// ---------------------------------------------------------------------------
static void
render_download_modal(GlobalState *AppState)
{
	ModelDownloadState *D = &AppState->Ui.Download;

	poll_model_download(AppState);

	if (D->JustFinished)
	{
		bool Succ = D->Succeeded.load();
		bool Fail = D->Failed.load();
		bool Cancel = D->CancelRequested.load();
		D->JustFinished = false;

		if (Succ)
		{
			if (D->CurrentModelName != VAD_MODEL_DISPLAY_NAME) query_available_stt_models(AppState);
			std::string Msg = std::string("Downloaded ") + D->CurrentModelName;
			show_success_toast(AppState, Msg.c_str());
		}
		else if (Fail && !Cancel)
		{
			std::string Msg = std::string("Failed to download ") + D->CurrentModelName;
			show_toast(AppState, Msg.c_str());
		}
	}

	if (!D->IsModalOpen) return;

	float LongestNameW = 0.0f;
	for (const CatalogModel &M : get_model_catalog())
	{
		float W = ImGui::CalcTextSize(M.Name).x;
		if (W > LongestNameW) LongestNameW = W;
	}
	{
		float W = ImGui::CalcTextSize(VAD_MODEL_DISPLAY_NAME).x;
		if (W > LongestNameW) LongestNameW = W;
	}
	const float ModelColW = LongestNameW + 50.0f;
	const float SizeColW = 80.0f;
	const float ActionColW = 130.0f;

	ImVec2 Display = ImGui::GetIO().DisplaySize;
	const ImGuiStyle &Style = ImGui::GetStyle();
	float ContentW = ModelColW + SizeColW + ActionColW + Style.CellPadding.x * 2.0f * 3.0f;
	float WinW = ContentW + Style.WindowPadding.x * 2.0f + Style.ScrollbarSize;
	if (WinW > Display.x * 0.95f) WinW = Display.x * 0.95f;
	ImGui::SetNextWindowSize(ImVec2(WinW, Display.y * 0.7f), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

	bool Open = true;
	if (ImGui::Begin("Download Models", &Open, ImGuiWindowFlags_NoCollapse))
	{
		bool Running = D->IsRunning.load();
		if (Running)
		{
			ImGui::Text("Downloading %s...", D->CurrentModelName.c_str());

			int64_t Done = D->DownloadedBytes.load();
			int64_t Total = D->TotalBytes.load();
			float Frac = (Total > 0) ? (float)((double)Done / (double)Total) : 0.0f;
			if (Frac > 1.0f) Frac = 1.0f;
			ImGui::ProgressBar(Frac, ImVec2(-1.0f, 0.0f));

			ImGui::Text("%s / %s", format_bytes(Done).c_str(), format_bytes(Total).c_str());

			ImGui::Spacing();
			if (ImGui::Button("Cancel Download")) cancel_model_download(AppState);
		}
		else
		{
			ImGui::TextDisabled("Source: huggingface.co/ggerganov/whisper.cpp");
			ImGui::Spacing();

			if (ImGui::BeginTable("##CatalogTable", 3,
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
			ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthFixed, ModelColW);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, SizeColW);
			ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed, ActionColW);
				ImGui::TableHeadersRow();

				for (const CatalogModel &M : get_model_catalog())
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(M.Name);
					ImGui::TableSetColumnIndex(1);
					ImGui::TextDisabled("%s", format_bytes(M.SizeBytes).c_str());
					ImGui::TableSetColumnIndex(2);

					std::string Filename = catalog_model_filename(M.Name);
					bool Installed = model_filename_installed(AppState, Filename);
					std::string Label = Installed ? "Re-download" : "Download";
					Label += "##cat-";
					Label += M.Name;
					if (ImGui::Button(Label.c_str(), ImVec2(-1.0f, 0.0f)))
					{
						D->PendingModelName = M.Name;
						D->PendingUrl = catalog_model_url(M.Name);
						std::string SttDir = platform_join_path(platform_get_exe_dir(), "stt_models");
						D->PendingDestPath = platform_join_path(SttDir, Filename);
						D->PendingSize = M.SizeBytes;
						if (Installed) D->WantsOverwriteConfirm = true;
						else
						{
							platform_ensure_directory(platform_join_path(platform_get_exe_dir(), "stt_models"));
							start_model_download(AppState, M.Name, D->PendingUrl, D->PendingDestPath, M.SizeBytes);
						}
					}
				}
				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("VAD Model (Voice Activity Detection) - source: huggingface.co/ggml-org/whisper-vad");
			ImGui::Spacing();

			if (ImGui::BeginTable("##VadTable", 3,
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthFixed, ModelColW);
				ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, SizeColW);
				ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed, ActionColW);
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(VAD_MODEL_DISPLAY_NAME);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%s", format_bytes(VAD_MODEL_SIZE_BYTES).c_str());
				ImGui::TableSetColumnIndex(2);

				bool VadInstalled = vad_model_installed(AppState);
				std::string VadLabel = VadInstalled ? "Re-download##vad" : "Download##vad";
				if (ImGui::Button(VadLabel.c_str(), ImVec2(-1.0f, 0.0f)))
				{
					D->PendingModelName = VAD_MODEL_DISPLAY_NAME;
					D->PendingUrl = vad_model_url();
					std::string VadDir = platform_join_path(platform_get_exe_dir(), "vad_models");
					D->PendingDestPath = platform_join_path(VadDir, VAD_MODEL_FILENAME);
					D->PendingSize = VAD_MODEL_SIZE_BYTES;
					if (VadInstalled) D->WantsOverwriteConfirm = true;
					else
					{
						platform_ensure_directory(VadDir);
						start_model_download(AppState, VAD_MODEL_DISPLAY_NAME, D->PendingUrl, D->PendingDestPath, VAD_MODEL_SIZE_BYTES);
					}
				}
				ImGui::EndTable();
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Close")) D->IsModalOpen = false;

		if (D->WantsOverwriteConfirm)
		{
			ImGui::OpenPopup("Overwrite Model?");
			D->WantsOverwriteConfirm = false;
		}

		ImVec2 Center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
		ImGui::SetNextWindowPos(Center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal("Overwrite Model?", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
		{
			ImGui::Text("Model '%s' already exists.", D->PendingModelName.c_str());
			ImGui::TextWrapped("Downloading will overwrite the existing weights file. This cannot be undone.");
			ImGui::Spacing();

			float AvailWidth = ImGui::GetContentRegionAvail().x;
			float Spacing = ImGui::GetStyle().ItemSpacing.x;
			float BtnW = (AvailWidth - Spacing) * 0.5f;

			if (ImGui::Button("Overwrite", ImVec2(BtnW, 0)))
			{
				std::string Name = D->PendingModelName;
				std::string Url = D->PendingUrl;
				std::string Dest = D->PendingDestPath;
				int64_t Size = D->PendingSize;
				ImGui::CloseCurrentPopup();
				size_t Slash = Dest.find_last_of("\\/");
				if (Slash != std::string::npos) platform_ensure_directory(Dest.substr(0, Slash));
				start_model_download(AppState, Name, Url, Dest, Size);
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(BtnW, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();

	if (!Open) D->IsModalOpen = false;
}

// ---------------------------------------------------------------------------
// Main Window
// ---------------------------------------------------------------------------
inline void
render_main_ui(GlobalState *AppState, ImGuiIO &Io)
{
	ImGui::GetStyle()._NextFrameFontSizeBase = (float)AppState->UiFontSize;

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

	const float BottomBarEndY = ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y;
	ImGui::SetCursorPos(ImVec2(Padding, BottomBarEndY));
	ImGui::SetNextItemWidth(-1.0f);
	render_transcribed_text_box(AppState);

	render_crash_dialog_ui(AppState);

	ImGui::End();

	render_download_modal(AppState);
	render_update_modal(AppState);
	render_toast_ui(AppState, Io);
}
