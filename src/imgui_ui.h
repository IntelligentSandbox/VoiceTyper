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
#include <cstring>

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
// Modal helper: close the current modal popup when clicking outside of it
// ---------------------------------------------------------------------------
static void
modal_close_on_click_outside(bool *IsOpenFlag)
{
	if (ImGui::IsWindowAppearing()) return;
	if (ImGui::IsWindowHovered()) return;
	if (!ImGui::IsMouseClicked(0)) return;

	*IsOpenFlag = false;
	ImGui::CloseCurrentPopup();
}

// ---------------------------------------------------------------------------
// Reusable help mark: small circled '?' that shows a tooltip when hovered.
// Laid out inline like any other item (call ImGui::SameLine() before it).
// ---------------------------------------------------------------------------
struct HelpMarkStyle
{
	float DiameterScale;  // circle diameter as a multiple of the current font size
	float OutlineThickness;
	float TooltipWrapEm;  // tooltip wrap width as a multiple of the current font size
	ImVec4 MarkColor;     // circle outline and '?' glyph
	ImVec4 HoverColor;    // circle outline and '?' glyph while hovered
};

static HelpMarkStyle
help_mark_default_style()
{
	HelpMarkStyle Style = {};
	Style.DiameterScale = 1.3f;
	Style.OutlineThickness = 1.5f;
	Style.TooltipWrapEm = 35.0f;
	Style.MarkColor = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
	Style.HoverColor = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
	return Style;
}

static void
hover_help_mark(const char *HelpText, const HelpMarkStyle &Style = help_mark_default_style())
{
	float FontSize = ImGui::GetFontSize();
	float Diameter = FontSize * Style.DiameterScale;

	ImVec2 Pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(Diameter, Diameter));
	bool Hovered = ImGui::IsItemHovered();

	ImU32 Color32 = ImGui::ColorConvertFloat4ToU32(Hovered ? Style.HoverColor : Style.MarkColor);
	ImVec2 Center = ImVec2(Pos.x + Diameter * 0.5f, Pos.y + FontSize * 0.5f);
	ImDrawList *Draw = ImGui::GetWindowDrawList();
	Draw->AddCircle(Center, Diameter * 0.5f, Color32, 16, Style.OutlineThickness);

	// Hand-drawn '?' with a strictly vertical stem so it never inherits any
	// slant from the loaded font: a top bowl arc, a hook curving into a
	// vertical stem, and a dot below.
	const float Pi = 3.14159265f;
	float GlyphH = Diameter * 0.55f;
	float BowlR = GlyphH * 0.26f;
	ImVec2 BowlCenter = ImVec2(Center.x, Center.y - GlyphH * 0.5f + BowlR);
	ImVec2 HookCenter = ImVec2(Center.x + BowlR * 0.45f, BowlCenter.y + BowlR * 0.50f);
	float BowlA0 = Pi * 165.0f / 180.0f;
	float BowlA1 = Pi * 15.0f / 180.0f + Pi * 2.0f;
	ImVec2 BowlEnd = ImVec2(BowlCenter.x + BowlR * cosf(BowlA1), BowlCenter.y + BowlR * sinf(BowlA1));
	float HookDx = BowlEnd.x - HookCenter.x;
	float HookDy = BowlEnd.y - HookCenter.y;
	float HookR = sqrtf(HookDx * HookDx + HookDy * HookDy);
	float HookA0 = atan2f(HookDy, HookDx);
	ImVec2 StemTop = ImVec2(Center.x, BowlCenter.y + BowlR * 0.85f);
	float HookA1 = atan2f(StemTop.y - HookCenter.y, StemTop.x - HookCenter.x);
	ImVec2 StemBottom = ImVec2(Center.x, StemTop.y + GlyphH * 0.24f);
	ImVec2 DotCenter = ImVec2(Center.x, StemBottom.y + GlyphH * 0.17f);

	Draw->PathArcTo(BowlCenter, BowlR, BowlA0, BowlA1);
	Draw->PathStroke(Color32, 0, Style.OutlineThickness);
	Draw->PathArcTo(HookCenter, HookR, HookA0, HookA1);
	Draw->PathStroke(Color32, 0, Style.OutlineThickness);
	Draw->AddLine(StemTop, StemBottom, Color32, Style.OutlineThickness);
	Draw->AddCircleFilled(DotCenter, GlyphH * 0.07f, Color32);

	if (Hovered)
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * Style.TooltipWrapEm);
		ImGui::TextUnformatted(HelpText);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

// ---------------------------------------------------------------------------
// UI theme (light/dark)
// ---------------------------------------------------------------------------
static void
apply_ui_theme(GlobalState *AppState)
{
	if (AppState->Ui.LightMode) ImGui::StyleColorsLight();
	else ImGui::StyleColorsDark();
}

static void
toggle_ui_theme(GlobalState *AppState)
{
	AppState->Ui.LightMode = !AppState->Ui.LightMode;
	save_bool_setting("ui_light_mode", AppState->Ui.LightMode);
	apply_ui_theme(AppState);
	platform_apply_window_theme(AppState->Platform.OwnWindow, AppState->Ui.LightMode);
}

// ---------------------------------------------------------------------------
// Theme toggle: small Sun/Moon button. It always shows the mode clicking will
// switch TO: a sun while in dark mode, a moon while in light mode.
// ---------------------------------------------------------------------------
static void
theme_toggle_button(GlobalState *AppState)
{
	float FontSize = ImGui::GetFontSize();
	float Diameter = FontSize * 1.35f;

	if (ImGui::InvisibleButton("##ThemeToggle", ImVec2(Diameter, Diameter)))
	{
		toggle_ui_theme(AppState);
	}

	bool Hovered = ImGui::IsItemHovered();
	ImDrawList *Draw = ImGui::GetWindowDrawList();
	ImVec2 Pos = ImGui::GetItemRectMin();
	ImVec2 Center = ImVec2(Pos.x + Diameter * 0.5f, Pos.y + Diameter * 0.5f);
	float Thickness = ImMax(1.5f, FontSize * 0.09f);
	const float Pi = 3.14159265f;

	if (Hovered)
	{
		Draw->AddCircleFilled(Center, Diameter * 0.62f,
			ImGui::GetColorU32(ImGuiCol_ButtonHovered, 0.35f));
	}

	if (AppState->Ui.LightMode)
	{
		// Moon: crescent outline from two arcs that meet at the cusps. The
		// outer arc wraps the left side of the circle; the inner arc (from a
		// center offset to the right) carves the bite, also via its left side.
		ImVec4 Color = Hovered ? ImVec4(0.13f, 0.16f, 0.35f, 1.0f)
			: ImVec4(0.30f, 0.34f, 0.52f, 1.0f);
		ImU32 Color32 = ImGui::ColorConvertFloat4ToU32(Color);

		float R = Diameter * 0.36f;
		float CuspX = 0.5f;   // cos(60 deg)
		float CuspY = 0.866f; // sin(60 deg)
		ImVec2 CuspTop = ImVec2(Center.x + R * CuspX, Center.y - R * CuspY);
		ImVec2 CuspBottom = ImVec2(Center.x + R * CuspX, Center.y + R * CuspY);
		ImVec2 InnerC = ImVec2(Center.x + R * 0.62f, Center.y);
		float InnerR = sqrtf(
			(CuspTop.x - InnerC.x) * (CuspTop.x - InnerC.x) +
			(CuspTop.y - InnerC.y) * (CuspTop.y - InnerC.y));
		float InnerTopA = atan2f(CuspTop.y - InnerC.y, CuspTop.x - InnerC.x);
		float InnerBottomA = atan2f(CuspBottom.y - InnerC.y, CuspBottom.x - InnerC.x);

		Draw->PathArcTo(Center, R, Pi / 3.0f, Pi * 5.0f / 3.0f);
		Draw->PathStroke(Color32, 0, Thickness);
		Draw->PathArcTo(InnerC, InnerR, InnerBottomA - Pi * 2.0f, InnerTopA);
		Draw->PathStroke(Color32, 0, Thickness);
	}
	else
	{
		// Sun: filled disc with eight rays.
		ImVec4 Color = Hovered ? ImVec4(0.95f, 0.95f, 0.95f, 1.0f)
			: ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
		ImU32 Color32 = ImGui::ColorConvertFloat4ToU32(Color);

		float R = Diameter * 0.42f;
		Draw->AddCircleFilled(Center, R * 0.45f, Color32);
		for (int i = 0; i < 8; i++)
		{
			float A = (float)i * (Pi * 2.0f / 8.0f);
			Draw->AddLine(
				ImVec2(Center.x + cosf(A) * R * 0.62f, Center.y + sinf(A) * R * 0.62f),
				ImVec2(Center.x + cosf(A) * R * 0.88f, Center.y + sinf(A) * R * 0.88f),
				Color32, Thickness);
		}
	}

	if (Hovered)
	{
		ImGui::BeginTooltip();
		ImGui::TextUnformatted(
			AppState->Ui.LightMode ? "Switch to dark mode" : "Switch to light mode");
		ImGui::EndTooltip();
	}
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
	if (!AppState->RecordHotkey.is_valid()) return "Record";
	if (AppState->RecordHotkeyMode == RECORDING_HOTKEY_HOLD) return "Record (hold " + hotkey_to_label(AppState->RecordHotkey) + ")";

	return "Record (" + hotkey_to_label(AppState->RecordHotkey) + ")";
}

static std::string
cancel_record_button_idle_label(GlobalState *AppState)
{
	if (!AppState->CancelRecordHotkey.is_valid()) return "Cancel";
	return "Cancel (" + hotkey_to_label(AppState->CancelRecordHotkey) + ")";
}

static std::string
stream_button_idle_label(GlobalState *AppState)
{
	if (!AppState->StreamHotkey.is_valid()) return "Start Streaming";
	return "Start Streaming (" + hotkey_to_label(AppState->StreamHotkey) + ")";
}

static std::string
load_model_button_idle_label(GlobalState *AppState)
{
	if (!AppState->LoadModelHotkey.is_valid()) return "Load Selected STT Model";
	return "Load Selected STT Model (" + hotkey_to_label(AppState->LoadModelHotkey) + ")";
}

// ---------------------------------------------------------------------------
// Settings panel helpers (inline in left column)
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
	case 4:  return &AppState->PasteHotkey;
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
	case 4:  return "paste_hotkey";
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

enum HotkeyCaptureResult
{
	HOTKEY_CAPTURE_ACTIVE    = 0,
	HOTKEY_CAPTURE_CLEARED   = 1,
	HOTKEY_CAPTURE_COMMITTED = 2,
};

// Advances an in-progress hotkey capture. Returns HOTKEY_CAPTURE_CLEARED when
// Escape was pressed (callers clear or cancel their target) or
// HOTKEY_CAPTURE_COMMITTED with *OutCaptured once every held key is released.
static HotkeyCaptureResult
poll_hotkey_capture(HotkeyCaptureState *Capture, HotkeyConfig *OutCaptured)
{
	ImGui::SetNextFrameWantCaptureKeyboard(true);
	ImGui::ClearActiveID();

	if (app_key_is_down(APP_KEY_ESCAPE))
	{
		Capture->HasCapture = false;
		Capture->Captured = {};
		Capture->IsCapturing = false;
		Capture->PeakModifiers = 0;
		Capture->PeakVirtualKey = 0;
		Capture->ReleaseFrames = 0;
		ImGui::ClearActiveID();
		ImGui::SetNextFrameWantCaptureKeyboard(true);
		return HOTKEY_CAPTURE_CLEARED;
	}

	AppHotkeyModifiers Mods = poll_modifier_state();
	AppKeyCode Vk = poll_nonmodifier_key();

	if (Mods != 0 || Vk != APP_KEY_NONE)
	{
		Capture->PeakModifiers |= Mods;
		if (Vk != APP_KEY_NONE) Capture->PeakVirtualKey = Vk;
		Capture->ReleaseFrames = 0;

		Capture->Captured.Modifiers = Capture->PeakModifiers;
		Capture->Captured.VirtualKey = Capture->PeakVirtualKey;
		Capture->HasCapture = true;
	}
	else if (Capture->PeakModifiers != 0 || Capture->PeakVirtualKey != 0)
	{
		Capture->ReleaseFrames++;
		if (Capture->ReleaseFrames >= 10)
		{
			Capture->Captured.Modifiers = Capture->PeakModifiers;
			Capture->Captured.VirtualKey = Capture->PeakVirtualKey;
			Capture->HasCapture = true;

			Capture->IsCapturing = false;
			Capture->PeakModifiers = 0;
			Capture->PeakVirtualKey = 0;
			Capture->ReleaseFrames = 0;
			ImGui::ClearActiveID();
			ImGui::SetNextFrameWantCaptureKeyboard(true);

			*OutCaptured = Capture->Captured;
			return HOTKEY_CAPTURE_COMMITTED;
		}
	}

	return HOTKEY_CAPTURE_ACTIVE;
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

	if (!ImGui::IsPopupOpen("Check for Updates"))
	{
		ImGui::OpenPopup("Check for Updates");
	}

	ImVec2 Display = ImGui::GetIO().DisplaySize;
	float WinW = Display.x * 0.6f;
	if (WinW > 560.0f) WinW = 560.0f;
	ImGui::SetNextWindowSizeConstraints(ImVec2(WinW, 0.0f), ImVec2(Display.x * 0.95f, Display.y * 0.95f));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowBgAlpha(1.0f);

	bool Open = true;
	if (ImGui::BeginPopupModal("Check for Updates", &Open,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
	{
		modal_close_on_click_outside(&U->IsModalOpen);
		ImGui::TextDisabled("v%s", VERSION_FULL);
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
		else
		{
			bool Checking = U->CheckRunning.load();
			bool Succeeded = U->CheckSucceeded.load();
			const char *CheckLabel = (Succeeded || U->CheckFailed.load()) ?
				"Check again" : "Check for updates";

			if (Checking) ImGui::BeginDisabled();
			if (ImGui::Button(CheckLabel))
			{
				start_update_check(AppState);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Releases page"))
			{
				platform_open_url(U->ReleaseUrl.empty() ? UPDATER_RELEASES_URL : U->ReleaseUrl.c_str());
			}

			if (Checking)
			{
				ImGui::TextDisabled("Checking for updates...");
			}
			else if (U->CheckFailed.load())
			{
				ImGui::Text("Update check failed (GitHub unreachable?)");
			}

			if (Succeeded)
			{
				if (U->Assets.empty())
				{
					if (!Checking) ImGui::TextDisabled("No matching downloads for this OS.");
				}
				else
				{
					if (!Checking)
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
			if (Checking) ImGui::EndDisabled();
		}

		ImGui::TextDisabled("%s", platform_is_installed_build() ?
			"Installed (MSI) build detected" : "Portable build detected");

		if (U->CheckSucceeded.load() && !U->CheckRunning.load() &&
			U->IsNewerAvailable && !U->NewerReleases.empty())
		{
			ImGui::Separator();
			ImGui::TextDisabled("Changelog");
			ImGui::Spacing();

			for (const UpdateChangelogEntry &Entry : U->NewerReleases)
			{
				ImGui::TextUnformatted(Entry.Version.c_str());

				const char *LineStart = Entry.Notes.c_str();
				for (;;)
				{
					const char *Nl = strchr(LineStart, '\n');
					if (!Nl)
					{
						ImGui::TextWrapped("%s", LineStart);
						break;
					}
					ImGui::TextWrapped("%.*s", (int)(Nl - LineStart), LineStart);
					LineStart = Nl + 1;
				}

				ImGui::Spacing();
			}
		}

		ImGui::EndPopup();
	}

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

static void
whisper_prompt_apply(GlobalState *AppState, const char *Prompt)
{
	std::string Trimmed = Prompt ? Prompt : "";
	size_t Start = Trimmed.find_first_not_of(" \t");
	size_t End = Trimmed.find_last_not_of(" \t");
	if (Start == std::string::npos) Trimmed.clear();
	else Trimmed = Trimmed.substr(Start, End - Start + 1);

	{
		std::lock_guard<std::mutex> Lock(AppState->WhisperInitialPromptMutex);
		AppState->WhisperInitialPrompt = Trimmed;
	}
	save_string_setting("whisper_initial_prompt", Trimmed.c_str());
}

// Trims the input; returns "" when it is not a usable settings key fragment
// (empty, too long, or containing characters the settings.ini parser cannot
// round-trip).
static std::string
paste_override_process_name_from_input(const char *Input)
{
	std::string Name = Input ? Input : "";
	size_t Start = Name.find_first_not_of(" \t");
	size_t End = Name.find_last_not_of(" \t");
	if (Start == std::string::npos) return "";
	Name = Name.substr(Start, End - Start + 1);

	if (Name.size() > 96) return "";
	for (size_t i = 0; i < Name.size(); i++)
	{
		char Ch = Name[i];
		if (Ch == '=' || Ch == '\n' || Ch == '\r') return "";
	}
	return Name;
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

	ImGui::TextUnformatted("Font Name");
	ImGui::SameLine();
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
// Settings panel - rendered inline in the left column, below the left panel
// ---------------------------------------------------------------------------
static void
render_settings_panel(GlobalState *AppState)
{
	SettingsWindowState *S = &AppState->Ui.SettingsState;

	if (colored_button("Configure Keyboard Shortcuts", ImVec2(-1.0f, 30.0f), BUTTON_COLOR_GREY))
	{
		S->HotkeysModalOpen = true;
	}

	if (ImGui::Checkbox("Play sound when starting/stopping/cancelling recording",
		&AppState->PlayRecordSound))
	{
		save_bool_setting("play_record_sound", AppState->PlayRecordSound);
	}

	ImGui::SameLine();
	float ThemeButtonSize = ImGui::GetFontSize() * 1.35f;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ThemeButtonSize);
	theme_toggle_button(AppState);

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

	std::string CharByCharLabel = "Use character-by-character text injection instead of paste";
	if (ImGui::Checkbox(CharByCharLabel.c_str(), &AppState->UseCharByCharInjection))
	{
		save_bool_setting("use_char_by_char_injection", AppState->UseCharByCharInjection);
	}

	if (ImGui::Checkbox("If no other window is focused when recording finishes, copy transcription to clipboard",
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

	render_font_name_input(AppState);

	float NumInputWidth = ImGui::GetFontSize() * 5.5f;

	ImGui::TextUnformatted("Font Size");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(NumInputWidth);
	if (ImGui::InputInt("##UiFontSize", &AppState->UiFontSize, 1, 1))
	{
		if (AppState->UiFontSize < 8) AppState->UiFontSize = 8;
		if (AppState->UiFontSize > 72) AppState->UiFontSize = 72;
		save_int_setting("ui_font_size", AppState->UiFontSize);
	}

	ImGui::TextUnformatted("Initial Whisper Prompt");
	ImGui::SameLine();
	HelpMarkStyle InitialWhisperPromptMarkStyle = help_mark_default_style();
	InitialWhisperPromptMarkStyle.DiameterScale = 0.75f;
	hover_help_mark(
		"Guides the transcription model toward the vocabulary and style you use. "
		"Applied from the next recording or streaming session onwards. (e.g. \"C++, ImGui, ggml, CUDA...\")",
		InitialWhisperPromptMarkStyle);
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##WhisperInitialPrompt",
		"Vocabulary/style hint",
		S->WhisperPromptBuffer, sizeof(S->WhisperPromptBuffer));
	if (ImGui::IsItemDeactivated())
	{
		whisper_prompt_apply(AppState, S->WhisperPromptBuffer);
	}

	float AvailWidth = ImGui::GetContentRegionAvail().x;
	float Spacing = ImGui::GetStyle().ItemSpacing.x;

	ImGui::Separator();

	float UtilityBtnWidth = (AvailWidth - Spacing) / 2;
	ImVec2 UtilityBtnSize = ImVec2(UtilityBtnWidth, 0.0f);

	if (colored_button("Copy Exe Dir Path", UtilityBtnSize, BUTTON_COLOR_GREY))
	{
		std::string ExeDir = platform_get_data_dir();
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

	float WrapW = ImGui::GetFontSize() * 26.0f;
	ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + WrapW);
	ImGui::TextUnformatted(
		"VoiceTyper appears to have crashed on a previous run. "
		"A crash report has been saved next to the executable that the developer "
		"can use to diagnose the problem.");

	ImGui::Separator();

	ImGui::Text("Crash report file(s):");
	ImGui::Spacing();

	for (const std::string &Path : AppState->Ui.PendingCrashDumps)
	{
		ImGui::TextUnformatted(Path.c_str());
	}
	ImGui::PopTextWrapPos();

	ImGui::Separator();

	float Spacing = ImGui::GetStyle().ItemSpacing.x;
	float BtnWidth = (WrapW - Spacing) / 2;
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
// Hotkeys modal - global shortcuts and per-program paste overrides
// ---------------------------------------------------------------------------
static void
render_hotkeys_modal(GlobalState *AppState)
{
	SettingsWindowState *S = &AppState->Ui.SettingsState;

	if (!S->HotkeysModalOpen)
	{
		if (S->Capture.IsCapturing)
		{
			S->Capture.IsCapturing = false;
		}
		if (S->PasteOverrideCapture.IsCapturing)
		{
			S->PasteOverrideCapture.IsCapturing = false;
			S->PasteOverrideCaptureProcess.clear();
		}
		return;
	}

	if (!ImGui::IsPopupOpen("Configure Keyboard Shortcuts"))
	{
		ImGui::OpenPopup("Configure Keyboard Shortcuts");
	}

	ImVec2 Display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowSizeConstraints(ImVec2(Display.x * 0.4f, 0.0f), ImVec2(Display.x * 0.95f, Display.y * 0.95f));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowBgAlpha(1.0f);

	bool Open = true;
	if (ImGui::BeginPopupModal("Configure Keyboard Shortcuts", &Open,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
	{
		modal_close_on_click_outside(&S->HotkeysModalOpen);

		ImGui::Text("Actions");
		ImGui::SameLine();
		HelpMarkStyle ShortcutsMarkStyle = help_mark_default_style();
		ShortcutsMarkStyle.DiameterScale = 0.75f;
		hover_help_mark(
			"Select an action below, then click the box and press your desired combination. "
			"Modifier-only combos (e.g. Ctrl+Alt) are supported. Escape clears the selected shortcut.",
			ShortcutsMarkStyle);

		float AvailWidth = ImGui::GetContentRegionAvail().x;
		float Spacing = ImGui::GetStyle().ItemSpacing.x;
		float BtnWidth = (AvailWidth - Spacing * 4) / 5;
		ImVec2 ActionSize = ImVec2(BtnWidth, 40);

		const char *ActionLabels[] = { "Record", "Cancel Record", "Stream", "Load Model", "Paste Text" };
		for (int i = 0; i < 5; i++)
		{
			if (i > 0) ImGui::SameLine();
			ImVec4 Color = (S->SelectedAction == i) ? BUTTON_COLOR_BLUE : BUTTON_COLOR_GREY;
			if (colored_button(ActionLabels[i], ActionSize, Color)) settings_select_action(AppState, i);
		}

		HotkeyConfig *CurrentHotkey = settings_action_hotkey_ptr(AppState, S->SelectedAction);
		if (CurrentHotkey)
		{
			std::string CurrentLabel = CurrentHotkey->is_valid() ? hotkey_to_label(*CurrentHotkey) : "(none)";
			ImGui::Text("Current: %s", CurrentLabel.c_str());
		}

		if (S->Capture.IsCapturing)
		{
			HotkeyConfig Captured = {};
			HotkeyCaptureResult CaptureResult = poll_hotkey_capture(&S->Capture, &Captured);
			if (CaptureResult == HOTKEY_CAPTURE_CLEARED)
			{
				HotkeyConfig *H = settings_action_hotkey_ptr(AppState, S->SelectedAction);
				if (H) *H = {};
				settings_save_action_hotkey(AppState, S->SelectedAction);
			}
			else if (CaptureResult == HOTKEY_CAPTURE_COMMITTED)
			{
				HotkeyConfig *H = settings_action_hotkey_ptr(AppState, S->SelectedAction);
				if (H) *H = Captured;
				settings_save_action_hotkey(AppState, S->SelectedAction);
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

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::TextUnformatted("Per Program Paste Key Combo");
		ImGui::SameLine();

        HelpMarkStyle HotkeyMarkStyle = help_mark_default_style();
        HotkeyMarkStyle.DiameterScale = 0.75f;
		hover_help_mark(
			"Override the paste text keyboard shortcut for individual programs, matched by executable name. "
			"Click a shortcut to change it, X to remove it.",
            HotkeyMarkStyle);
		ImGui::SameLine();
		std::string DefaultPasteLabel = AppState->PasteHotkey.is_valid()
			? hotkey_to_label(AppState->PasteHotkey) : "(none)";
		ImGui::TextDisabled("(default: %s)", DefaultPasteLabel.c_str());
		ImGui::Spacing();

		std::vector<PasteHotkeyOverride> Overrides;
		{
			std::lock_guard<std::mutex> Lock(AppState->PasteHotkeyOverridesMutex);
			Overrides = AppState->PasteHotkeyOverrides;
		}

		if (Overrides.empty())
		{
			ImGui::TextDisabled("(none yet)");
		}

		std::string ForgetOverrideName;
		for (const PasteHotkeyOverride &Override : Overrides)
		{
			ImGui::PushID(Override.ProcessName.c_str());
			ImGui::TextUnformatted(Override.ProcessName.c_str());
			ImGui::SameLine();
			std::string ChangeLabel = hotkey_to_label(Override.Hotkey) + "##Change";
			if (colored_button(ChangeLabel.c_str(), ImVec2(0.0f, 0.0f), BUTTON_COLOR_GREY))
			{
				S->PasteOverrideCaptureProcess = Override.ProcessName;
				S->PasteOverrideCapture.Captured = {};
				S->PasteOverrideCapture.HasCapture = false;
				S->PasteOverrideCapture.IsCapturing = true;
				S->PasteOverrideCapture.PeakModifiers = 0;
				S->PasteOverrideCapture.PeakVirtualKey = 0;
				S->PasteOverrideCapture.ReleaseFrames = 0;
			}
			ImGui::SameLine();
			if (colored_button("X##Forget", ImVec2(0.0f, 0.0f), BUTTON_COLOR_RED))
			{
				ForgetOverrideName = Override.ProcessName;
			}
			ImGui::PopID();
		}
		if (!ForgetOverrideName.empty())
		{
			if (S->PasteOverrideCapture.IsCapturing && S->PasteOverrideCaptureProcess == ForgetOverrideName)
			{
				S->PasteOverrideCapture.IsCapturing = false;
				S->PasteOverrideCaptureProcess.clear();
			}
			remove_paste_hotkey_override(AppState, ForgetOverrideName);
		}

		if (S->PasteOverrideCapture.IsCapturing)
		{
			HotkeyConfig Captured = {};
			HotkeyCaptureResult CaptureResult = poll_hotkey_capture(&S->PasteOverrideCapture, &Captured);
			if (CaptureResult == HOTKEY_CAPTURE_CLEARED)
			{
				S->PasteOverrideCaptureProcess.clear();
			}
			else if (CaptureResult == HOTKEY_CAPTURE_COMMITTED)
			{
				upsert_paste_hotkey_override(AppState, S->PasteOverrideCaptureProcess, Captured);
				S->NewPasteOverrideProcess[0] = '\0';
				S->PasteOverrideCaptureProcess.clear();
			}
		}

		if (S->PasteOverrideCapture.IsCapturing)
		{
			std::string CaptureText;
			if (S->PasteOverrideCapture.HasCapture)
				CaptureText = hotkey_to_label(S->PasteOverrideCapture.Captured) + " for " +
					S->PasteOverrideCaptureProcess + "...";
			else
				CaptureText = "Press a key combination for " + S->PasteOverrideCaptureProcess + "...";

			if (colored_button(CaptureText.c_str(), ImVec2(-1.0f, 30.0f), ImVec4(0.08f, 0.40f, 0.75f, 1.0f)))
			{
				S->PasteOverrideCapture.IsCapturing = false;
				S->PasteOverrideCaptureProcess.clear();
			}
			ImGui::TextDisabled("Escape cancels.");
		}
		else
		{
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##NewPasteOverrideProcess", "Program executable name (e.g. Code.exe, WindowsTerminal.exe)",
				S->NewPasteOverrideProcess, sizeof(S->NewPasteOverrideProcess));
			std::string NewName = paste_override_process_name_from_input(S->NewPasteOverrideProcess);
			if (colored_button("Set Paste Hotkey for Program...", ImVec2(-1.0f, 30.0f), BUTTON_COLOR_GREY,
				!NewName.empty()))
			{
				S->PasteOverrideCaptureProcess = NewName;
				S->PasteOverrideCapture.Captured = {};
				S->PasteOverrideCapture.HasCapture = false;
				S->PasteOverrideCapture.IsCapturing = true;
				S->PasteOverrideCapture.PeakModifiers = 0;
				S->PasteOverrideCapture.PeakVirtualKey = 0;
				S->PasteOverrideCapture.ReleaseFrames = 0;
			}
		}

		ImGui::EndPopup();
	}

	if (!Open) S->HotkeysModalOpen = false;
}

// ---------------------------------------------------------------------------
// Left column - recording controls and model selectors
// ---------------------------------------------------------------------------
static std::string
format_timing_ms(double Ms)
{
	if (Ms < 0.0) return "\xe2\x80\x94";

	char Buf[64];
	if (Ms < 1000.0) snprintf(Buf, sizeof(Buf), "%.2f ms", Ms);
	else snprintf(Buf, sizeof(Buf), "%.2f s", Ms / 1000.0);
	return std::string(Buf);
}

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
			if (AppState->RecordHotkey.is_valid())
			{
				Label = "Stop (" + hotkey_to_label(AppState->RecordHotkey) + ")";
			}
			else
			{
				Label = "Stop";
			}
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
			if (AppState->StreamHotkey.is_valid())
			{
				Label = "Stop Streaming (" + hotkey_to_label(AppState->StreamHotkey) + ")";
			}
			else
			{
				Label = "Stop Streaming";
			}
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
			if (AppState->LoadModelHotkey.is_valid())
			{
				Label = "Unload STT Model (" + hotkey_to_label(AppState->LoadModelHotkey) + ")";
			}
			else
			{
				Label = "Unload STT Model";
			}
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

		if (AppState->CurrentInferenceDeviceIndex == 0)
		{
			ImGui::Text("CPU Cores for Inference");
			ImGui::SetNextItemWidth(FullWidth.x);
			int MaxCores = query_logical_processor_count();
			if (ImGui::InputInt("##ThreadCount", &AppState->WhisperThreadCount, 1, 1))
			{
				if (AppState->WhisperThreadCount < 1) AppState->WhisperThreadCount = 1;
				if (AppState->WhisperThreadCount > MaxCores) AppState->WhisperThreadCount = MaxCores;
			}
		}
	}
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

static ImVec4
transcribed_word_confidence_color(float Confidence)
{
	if (Confidence >= 0.85f) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	if (Confidence >= 0.60f) return ImVec4(0.92f, 0.78f, 0.30f, 1.0f);
	return ImVec4(0.92f, 0.35f, 0.35f, 1.0f);
}

// True when a screen position lies over the laid-out transcription text,
// i.e. within the horizontal span of any wrapped line of words.
static bool
transcribed_text_pos_over_text(const std::vector<ImVec4> &WordRects, const ImVec2 &Pos)
{
	const int Count = (int)WordRects.size();
	if (Count == 0) return false;
	if (Pos.y < WordRects[0].y || Pos.y > WordRects[Count - 1].w) return false;

	int LinePivot = 0;
	for (int i = 0; i < Count; i++)
	{
		if (WordRects[i].y <= Pos.y) LinePivot = i;
	}
	const float LineY = WordRects[LinePivot].y;
	int LineBegin = LinePivot;
	while (LineBegin > 0 && ImFabs(WordRects[LineBegin - 1].y - LineY) < 0.5f) LineBegin--;
	int LineEnd = LinePivot;
	while (LineEnd + 1 < Count && ImFabs(WordRects[LineEnd + 1].y - LineY) < 0.5f) LineEnd++;

	return Pos.x >= WordRects[LineBegin].x && Pos.x <= WordRects[LineEnd].z &&
		Pos.y >= WordRects[LineBegin].y && Pos.y <= WordRects[LineBegin].w;
}

static void
transcribed_text_copy_all(GlobalState *AppState)
{
	UiRuntimeState *Ui = &AppState->Ui;
	if (Ui->TranscribedTextBoxBuffer.empty()) return;

	std::string Text(Ui->TranscribedTextBoxBuffer.data());
	size_t Start = Text.find_first_not_of(" \t\r\n");
	size_t End = Text.find_last_not_of(" \t\r\n");
	if (Start == std::string::npos) return;
	Text = Text.substr(Start, End - Start + 1);
	if (!Text.empty())
	{
		ImGui::SetClipboardText(Text.c_str());
		show_success_toast(AppState, "Transcription copied to clipboard");
	}
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
	ImGui::SameLine();
	HelpMarkStyle TimingsMarkStyle = help_mark_default_style();
	TimingsMarkStyle.DiameterScale = 0.75f;
	std::string TimingsTooltip =
		"Model load: " + format_timing_ms(AppState->LastModelLoadMs.load()) + "\n" +
		"Transcription: " + format_timing_ms(AppState->LastTranscriptionMs.load()) + "\n" +
		"Paste: " + format_timing_ms(AppState->LastPasteMs.load());
	hover_help_mark(TimingsTooltip.c_str(), TimingsMarkStyle);

	const float BoxHeight = ImGui::GetTextLineHeightWithSpacing() * 6.0f +
		ImGui::GetStyle().FramePadding.y * 2.0f;

	std::vector<ImVec4> WordRects;
	WordRects.reserve(Ui->TranscribedTextBoxWords.size());

	if (ImGui::BeginChild("##TranscribedTextConfidence", ImVec2(-1.0f, BoxHeight),
		ImGuiChildFlags_AlwaysUseWindowPadding))
	{
		// AllowWhenBlockedByActiveItem keeps hover alive mid-drag (the reason
		// raw mouse-rect math was used before), while IsWindowHovered() still
		// reports false when a modal/popup is open or a higher window (e.g. a
		// modal sitting on top of the box) covers the pointer.
		const bool WindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		const ImVec2 Mouse = ImGui::GetMousePos();
		const int WordCount = (int)Ui->TranscribedTextBoxWords.size();

		// Match the InputTextMultiline line pitch (no spacing between wrapped lines)
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
			ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

		float WrapRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
		float LineEndX = ImGui::GetCursorPosX();
		bool LineStarted = false;
		for (size_t Wi = 0; Wi < Ui->TranscribedTextBoxWords.size(); Wi++)
		{
			const TranscribedWord &Word = Ui->TranscribedTextBoxWords[Wi];
			const char *Text = Word.Text.c_str();
			if (Wi == 0) while (Text[0] == ' ') Text++;
			float WordW = ImGui::CalcTextSize(Text).x;
			if (LineStarted && LineEndX + WordW <= WrapRight) ImGui::SameLine(0.0f, 0.0f);
			else LineEndX = ImGui::GetCursorPosX();

			ImGui::PushStyleColor(ImGuiCol_Text, transcribed_word_confidence_color(Word.Confidence));
			ImGui::TextUnformatted(Text);
			ImGui::PopStyleColor();

			ImVec2 RectMin = ImGui::GetItemRectMin();
			ImVec2 RectMax = ImGui::GetItemRectMax();
			WordRects.push_back(ImVec4(RectMin.x, RectMin.y, RectMax.x, RectMax.y));

			LineEndX += WordW;
			LineStarted = true;
		}
		ImGui::PopStyleVar();

		const bool MouseOverText = WindowHovered && WordCount > 0 &&
			transcribed_text_pos_over_text(WordRects, Mouse);

		if (MouseOverText) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

		if (MouseOverText && ImGui::IsMouseClicked(0))
		{
			transcribed_text_copy_all(AppState);
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

		if (D->CurrentModelName != VAD_MODEL_DISPLAY_NAME) query_available_stt_models(AppState);

		if (Succ)
		{
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

	if (!ImGui::IsPopupOpen("Download Models"))
	{
		ImGui::OpenPopup("Download Models");
		D->ModalWidth = 0.0f;
	}

	float LongestNameW = 0.0f;
	float LongestSizeW = 0.0f;
	for (const CatalogModel &M : get_model_catalog())
	{
		float W = ImGui::CalcTextSize(M.Name).x;
		if (W > LongestNameW) LongestNameW = W;
		W = ImGui::CalcTextSize(format_bytes(M.SizeBytes).c_str()).x;
		if (W > LongestSizeW) LongestSizeW = W;
	}
	{
		float W = ImGui::CalcTextSize(VAD_MODEL_DISPLAY_NAME).x;
		if (W > LongestNameW) LongestNameW = W;
		W = ImGui::CalcTextSize(format_bytes(VAD_MODEL_SIZE_BYTES).c_str()).x;
		if (W > LongestSizeW) LongestSizeW = W;
	}
	const float ModelColW = LongestNameW + 50.0f;
	const float SizeColW = LongestSizeW + ImGui::GetStyle().CellPadding.x * 2.0f;
	const float ActionColW = (ImGui::CalcTextSize("Re-download").x + ImGui::GetStyle().FramePadding.x * 6.0f) * 2.0f +
		ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().CellPadding.x * 2.0f;

	ImVec2 Display = ImGui::GetIO().DisplaySize;
	const ImGuiStyle &Style = ImGui::GetStyle();
	float ContentW = ModelColW + SizeColW + ActionColW + Style.CellPadding.x * 2.0f * 3.0f;
	float WinW = ContentW + Style.WindowPadding.x * 2.0f + Style.ScrollbarSize;
	if (WinW > Display.x * 0.95f) WinW = Display.x * 0.95f;
	float MinW = WinW;
	if (D->ModalWidth > MinW && D->ModalWidth <= Display.x * 0.95f) MinW = D->ModalWidth;
	ImGui::SetNextWindowBgAlpha(1.0f);
	ImGui::SetNextWindowSizeConstraints(ImVec2(MinW, 0.0f), ImVec2(Display.x * 0.95f, Display.y * 0.95f));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	bool Open = true;
	if (ImGui::BeginPopupModal("Download Models", &Open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
	{
		if (!ImGui::IsPopupOpen("Overwrite Model?"))
			modal_close_on_click_outside(&D->IsModalOpen);
		bool Running = D->IsRunning.load();
		if (!Running) D->ModalWidth = ImGui::GetWindowWidth();
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
			ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("Model");
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted("Size");
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted("Actions");

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
				float AvailX = ImGui::GetContentRegionAvail().x;
				float Spacing = ImGui::GetStyle().ItemSpacing.x;
				float BtnW = Installed ? (AvailX - Spacing) * 0.5f : AvailX;
				if (ImGui::Button(Label.c_str(), ImVec2(BtnW, 0.0f)))
				{
					D->PendingModelName = M.Name;
					D->PendingUrl = catalog_model_url(M.Name);
					std::string SttDir = platform_join_path(platform_get_data_dir(), "stt_models");
					D->PendingDestPath = platform_join_path(SttDir, Filename);
					D->PendingSize = M.SizeBytes;
					if (Installed) D->WantsOverwriteConfirm = true;
					else
					{
						platform_ensure_directory(platform_join_path(platform_get_data_dir(), "stt_models"));
						start_model_download(AppState, M.Name, D->PendingUrl, D->PendingDestPath, M.SizeBytes);
					}
				}

				if (Installed)
				{
					ImGui::SameLine();
					std::string DeleteLabel = std::string("Delete##del-cat-") + M.Name;
					if (colored_button(DeleteLabel.c_str(), ImVec2(BtnW, 0.0f), BUTTON_COLOR_RED))
					{
						std::string SttDir = platform_join_path(platform_get_data_dir(), "stt_models");
						std::string Path = platform_join_path(SttDir, Filename);
						if (remove(Path.c_str()) == 0)
						{
							query_available_stt_models(AppState);
							std::string Msg = std::string("Deleted ") + M.Name;
							show_success_toast(AppState, Msg.c_str());
						}
						else
						{
							std::string Msg = std::string("Failed to delete ") + M.Name;
							show_toast(AppState, Msg.c_str());
						}
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
			ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(VAD_MODEL_DISPLAY_NAME);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%s", format_bytes(VAD_MODEL_SIZE_BYTES).c_str());
				ImGui::TableSetColumnIndex(2);

				bool VadInstalled = vad_model_installed(AppState);
				std::string VadLabel = VadInstalled ? "Re-download##vad" : "Download##vad";
				float AvailX = ImGui::GetContentRegionAvail().x;
				float Spacing = ImGui::GetStyle().ItemSpacing.x;
				float BtnW = VadInstalled ? (AvailX - Spacing) * 0.5f : AvailX;
				if (ImGui::Button(VadLabel.c_str(), ImVec2(BtnW, 0.0f)))
				{
					D->PendingModelName = VAD_MODEL_DISPLAY_NAME;
					D->PendingUrl = vad_model_url();
					std::string VadDir = platform_join_path(platform_get_data_dir(), "vad_models");
					D->PendingDestPath = platform_join_path(VadDir, VAD_MODEL_FILENAME);
					D->PendingSize = VAD_MODEL_SIZE_BYTES;
					if (VadInstalled) D->WantsOverwriteConfirm = true;
					else
					{
						platform_ensure_directory(VadDir);
						start_model_download(AppState, VAD_MODEL_DISPLAY_NAME, D->PendingUrl, D->PendingDestPath, VAD_MODEL_SIZE_BYTES);
					}
				}

				if (VadInstalled)
				{
					ImGui::SameLine();
					if (colored_button("Delete##del-vad", ImVec2(BtnW, 0.0f), BUTTON_COLOR_RED))
					{
						if (remove(AppState->VadModelPath.c_str()) == 0)
						{
							std::string Msg = std::string("Deleted ") + VAD_MODEL_DISPLAY_NAME;
							show_success_toast(AppState, Msg.c_str());
						}
						else
						{
							std::string Msg = std::string("Failed to delete ") + VAD_MODEL_DISPLAY_NAME;
							show_toast(AppState, Msg.c_str());
						}
					}
				}
				ImGui::EndTable();
			}
		}

		ImGui::Separator();

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
			float WrapW = ImGui::GetFontSize() * 26.0f;
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + WrapW);
			ImGui::TextUnformatted("Downloading will overwrite the existing weights file. This cannot be undone.");
			ImGui::PopTextWrapPos();
			ImGui::Spacing();

			float Spacing = ImGui::GetStyle().ItemSpacing.x;
			float BtnW = (WrapW - Spacing) * 0.5f;

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
		ImGui::EndPopup();
	}

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
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBringToFrontOnFocus);

	const float Padding = 16.0f;
	const bool TwoColumns = Io.DisplaySize.x >= (float)TWO_COLUMN_MIN_WIDTH;
	const float ColumnWidth = TwoColumns
		? (Io.DisplaySize.x - Padding * 3.0f) * 0.5f
		: (Io.DisplaySize.x - Padding * 2.0f);

	ImGui::SetCursorPos(ImVec2(Padding, Padding));
	if (TwoColumns)
	{
		ImGui::BeginChild("##LeftColumn", ImVec2(ColumnWidth, Io.DisplaySize.y - Padding * 2.0f));
	}
	else
	{
		ImGui::BeginChild("##LeftColumn", ImVec2(ColumnWidth, 0.0f),
			ImGuiChildFlags_AutoResizeY,
			ImGuiWindowFlags_NoScrollbar);
	}
	render_left_panel(AppState);
	ImGui::Separator();
	render_settings_panel(AppState);
	ImGui::EndChild();

	if (TwoColumns)
	{
		ImGui::SetCursorPos(ImVec2(Padding * 2.0f + ColumnWidth, Padding));
	}
	else
	{
		ImGui::SetCursorPos(ImVec2(Padding, ImGui::GetCursorPosY() + Padding));
	}
	ImGui::BeginChild("##RightColumn", ImVec2(ColumnWidth, 0.0f),
		ImGuiChildFlags_AutoResizeY,
		ImGuiWindowFlags_NoScrollbar);
	render_transcribed_text_box(AppState);
	ImGui::EndChild();

	render_download_modal(AppState);
	render_update_modal(AppState);
	render_crash_dialog_ui(AppState);
	render_hotkeys_modal(AppState);

	ImGui::End();

	render_toast_ui(AppState, Io);
}
