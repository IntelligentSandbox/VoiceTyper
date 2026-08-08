#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CatalogModel
{
	const char *Name;
	const char *Description;
	int64_t     SizeBytes;
	bool        IsEnglishOnly;
};

#define WHISPER_HF_BASE_URL "https://huggingface.co/ggerganov/whisper.cpp/resolve/main"

#define VAD_HF_BASE_URL        "https://huggingface.co/ggml-org/whisper-vad/resolve/main"
#define VAD_MODEL_FILENAME     "ggml-silero-v5.1.2.bin"
#define VAD_MODEL_DISPLAY_NAME "Silero VAD v5.1.2"
#define VAD_MODEL_SIZE_BYTES   (885ULL * 1024)

inline const std::vector<CatalogModel> &
get_model_catalog()
{
	static const std::vector<CatalogModel> Catalog = {
		{"tiny.en",        "Tiny English-only",        75ULL   * 1024 * 1024, true},
		{"tiny",           "Tiny multilingual",        75ULL   * 1024 * 1024, false},
		{"base.en",        "Base English-only",        148ULL  * 1024 * 1024, true},
		{"base",           "Base multilingual",        148ULL  * 1024 * 1024, false},
		{"small.en",       "Small English-only",       466ULL  * 1024 * 1024, true},
		{"small",          "Small multilingual",       466ULL  * 1024 * 1024, false},
		{"medium.en",      "Medium English-only",      1530ULL * 1024 * 1024, true},
		{"medium",         "Medium multilingual",      1530ULL * 1024 * 1024, false},
		{"large-v1",       "Large v1 multilingual",    3070ULL * 1024 * 1024, false},
		{"large-v2",       "Large v2 multilingual",    3070ULL * 1024 * 1024, false},
		{"large-v3",       "Large v3 multilingual",    3070ULL * 1024 * 1024, false},
		{"large-v3-turbo", "Large v3 Turbo",           1620ULL * 1024 * 1024, false},
	};
	return Catalog;
}

inline std::string
catalog_model_url(const std::string &Name)
{
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
