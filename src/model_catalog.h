#pragma once

#include "build_time_constants.h"

#include <string>

inline const std::vector<CatalogModel> &
get_model_catalog()
{
	return MODEL_CATALOG;
}

inline std::string
catalog_model_url(const std::string &Name)
{
	if (Name == "distil-large-v3.5")
	{
		return std::string(DISTIL_WHISPER_HF_BASE_URL) + "/ggml-model.bin";
	}
	if (Name.rfind("parakeet-", 0) == 0)
	{
		return std::string(PARAKEET_HF_BASE_URL) + "/ggml-" + Name + ".bin";
	}
	return std::string(WHISPER_HF_BASE_URL) + "/ggml-" + Name + ".bin";
}

inline std::string
vad_model_url()
{
	return std::string(VAD_HF_BASE_URL) + "/" + VAD_MODEL_FILENAME;
}

inline std::string
catalog_model_filename(const std::string &Name)
{
	return "ggml-" + Name + ".bin";
}

inline std::string
catalog_model_display_name(const std::string &Name)
{
	return Name;
}
