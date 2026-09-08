#pragma once

#include "host_services.h"
#include "model_catalog.h"
#include "settings.h"
#include "state.h"

#include <cstdio>
#include <string>
#include <vector>

inline void
query_vad_model_path(GlobalState *AppState)
{
	AppState->VadModelPath = platform_join_path(
		platform_join_path(platform_get_data_dir(), "vad_models"), VAD_MODEL_FILENAME);
}

inline bool
vad_model_installed(GlobalState *AppState)
{
	std::string Dir = platform_join_path(platform_get_data_dir(), "vad_models");
	std::vector<PlatformFileInfo> Files = platform_list_files(Dir);
	for (const PlatformFileInfo &File : Files)
	{
		if (File.Name == VAD_MODEL_FILENAME) return true;
	}
	return false;
}

inline void
cleanup_partial_model_downloads()
{
	const char *DirNames[] = { "stt_models", "vad_models" };
	for (const char *DirName : DirNames)
	{
		std::string Dir = platform_join_path(platform_get_data_dir(), DirName);
		std::vector<PlatformFileInfo> Files = platform_list_files(Dir);
		for (const PlatformFileInfo &File : Files)
		{
			if (File.Name.size() < 5 || File.Name.substr(File.Name.size() - 5) != ".part") continue;
			remove(platform_join_path(Dir, File.Name).c_str());
		}
	}
}

inline void
query_available_stt_models(GlobalState *AppState)
{
	AppState->STTModelNames.clear();
	AppState->STTModelPaths.clear();

	std::string Dir = platform_join_path(platform_get_data_dir(), "stt_models");
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

	std::string SavedModel;
	if (load_string_setting("stt_model", &SavedModel) && !SavedModel.empty())
	{
		for (int i = 0; i < (int)AppState->STTModelPaths.size(); i++)
		{
			const std::string &Path = AppState->STTModelPaths[i];
			size_t Slash = Path.find_last_of("\\/");
			std::string FileName = (Slash == std::string::npos) ? Path : Path.substr(Slash + 1);
			if (FileName == SavedModel)
			{
				AppState->CurrentSTTModelIndex = i;
				break;
			}
		}
	}
}
