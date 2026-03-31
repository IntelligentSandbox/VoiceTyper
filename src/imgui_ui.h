#pragma once

#include "imgui.h"
#include "state.h"

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

	bool Busy = AppState->IsRecording || AppState->IsStreaming;

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

		if (AppState->CaptureRunning.load() && !AppState->IsRecording)
		{
			Color = BUTTON_COLOR_GREY;
			Label = "Transcribing...";
			Enabled = false;
		}

		if (colored_button(Label.c_str(), BigButton, Color, Enabled))
		{
			// TODO: toggle_recording(AppState)
		}
	}

	// Cancel Record Button
	{
		bool Enabled = AppState->IsRecording;
		std::string Label = cancel_record_button_idle_label(AppState);

		if (colored_button(Label.c_str(), SmallButton, BUTTON_COLOR_GREY, Enabled))
		{
			// TODO: cancel_recording(AppState)
		}
	}

	// Stream Button
	{
		ImVec4 Color = BUTTON_COLOR_GREEN;
		std::string Label = stream_button_idle_label(AppState);
		bool Enabled = !AppState->IsRecording;

		if (AppState->IsStreaming)
		{
			Color = BUTTON_COLOR_RED;
			Label = "Stop Streaming (" + AppState->StreamHotkey.to_label() + ")";
		}

		if (colored_button(Label.c_str(), BigButton, Color, Enabled))
		{
			// TODO: toggle_streaming(AppState)
		}
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
			string_combo("##AudioInput", &Dummy, DeviceNames);
			ImGui::EndDisabled();
		}
		else
		{
			string_combo("##AudioInput", &AppState->CurrentAudioDeviceIndex, DeviceNames);
		}
	}

	// STT Model
	{
		ImGui::Text("STT Model");
		if (!AppState->AnySTTModelAvailable)
		{
			std::vector<std::string> NoModels = { "No Models Found" };
			int Dummy = 0;
			ImGui::BeginDisabled();
			string_combo("##STTModel", &Dummy, NoModels);
			ImGui::EndDisabled();
		}
		else
		{
			string_combo("##STTModel", &AppState->CurrentSTTModelIndex, AppState->STTModels);
		}
	}

	// Load Model Button
	{
		bool ModelLoaded = is_whisper_model_loaded(&AppState->WhisperState);
		ImVec4 Color = BUTTON_COLOR_GREY;
		std::string Label = load_model_button_idle_label(AppState);
		bool Enabled = AppState->AnySTTModelAvailable && !Busy;

		if (ModelLoaded)
		{
			Color = BUTTON_COLOR_BLUE;
			Label = "Unload STT Model (" + AppState->LoadModelHotkey.to_label() + ")";
		}

		if (colored_button(Label.c_str(), BigButton, Color, Enabled))
		{
			// TODO: toggle_stt_model_load(AppState)
		}
	}

	// Inference Device
	{
		ImGui::Text("Inference Device");
		string_combo("##InferenceDevice", &AppState->CurrentInferenceDeviceIndex,
			AppState->InferenceDevices);
	}

	ImGui::Separator();

	// Settings Button
	{
		bool Enabled = !Busy;
		if (colored_button("Settings", SmallButton, BUTTON_COLOR_GREY, Enabled))
		{
			// TODO: open_settings_window(AppState)
		}
	}

	ImGui::End();
}
