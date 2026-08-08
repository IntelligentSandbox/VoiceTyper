#pragma once

#include "host_services.h"
#include "model_catalog.h"
#include "state.h"

#include <cstdio>
#include <string>
#include <vector>

#define VAD_MODEL_RELATIVE "vad_models/" VAD_MODEL_FILENAME

inline void
query_vad_model_path(GlobalState *AppState)
{
	AppState->VadModelPath = platform_join_path(platform_get_exe_dir(), VAD_MODEL_RELATIVE);
}

inline bool
vad_model_installed(GlobalState *AppState)
{
	std::string Dir = platform_join_path(platform_get_exe_dir(), "vad_models");
	std::vector<PlatformFileInfo> Files = platform_list_files(Dir);
	for (const PlatformFileInfo &File : Files)
	{
		if (File.Name == VAD_MODEL_FILENAME) return true;
	}
	return false;
}

inline void
query_available_stt_models(GlobalState *AppState)
{
	AppState->STTModelNames.clear();
	AppState->STTModelPaths.clear();

	std::string Dir = platform_join_path(platform_get_exe_dir(), "stt_models");
	std::vector<PlatformFileInfo> Files = platform_list_files(Dir);

	for (const PlatformFileInfo &File : Files)
	{
		if (File.Name.rfind("ggml-", 0) != 0) continue;
		if (File.Name.size() <= 4 || File.Name.substr(File.Name.size() - 4) != ".bin") continue;

		std::string FilePath = platform_join_path(Dir, File.Name);
		std::string DisplayName = File.Name.substr(5);
		DisplayName = DisplayName.substr(0, DisplayName.size() - 4);

		char SizeBuf[32];
		if (File.SizeBytes >= 1073741824) snprintf(SizeBuf, sizeof(SizeBuf), "%.1f GB", File.SizeBytes / 1073741824.0);
		else snprintf(SizeBuf, sizeof(SizeBuf), "%d MB", (int)(File.SizeBytes / 1048576));

		std::string Label = DisplayName + " (" + SizeBuf + ")";

		AppState->STTModelNames.push_back(Label);
		AppState->STTModelPaths.push_back(FilePath);
	}

	AppState->CurrentSTTModelIndex = 0;
}
