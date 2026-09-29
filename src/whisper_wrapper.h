#pragma once

#include "whisper.h"
#include "parakeet.h"
#include "ggml-backend.h"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

enum EngineKind
{
	ENGINE_UNKNOWN,
	ENGINE_WHISPER,
	ENGINE_PARAKEET,
};

struct WhisperModelState
{
	EngineKind Kind;
	whisper_context *WhisperContext;
	parakeet_context *ParakeetContext;
	bool IsLoaded;
	int LoadedModelIndex;
	int LoadedInferenceDeviceIndex;
	std::string ModelPath;
};

inline void
init_stt_state(WhisperModelState *State)
{
	State->Kind = ENGINE_UNKNOWN;
	State->WhisperContext = nullptr;
	State->ParakeetContext = nullptr;
	State->IsLoaded = false;
	State->LoadedModelIndex = -1;
	State->LoadedInferenceDeviceIndex = -1;
	State->ModelPath = "";
}

// Both model formats share the ggml magic; the hparam block that follows
// differs structurally, which identifies the engine from file content alone.
// whisper (11 ints):  n_vocab n_audio_ctx n_audio_state n_audio_head n_audio_layer
//                     n_text_ctx n_text_state n_text_head n_text_layer n_mels ftype
// parakeet (15 ints): n_vocab n_audio_ctx n_audio_state n_audio_head n_audio_layer
//                     n_mels ftype n_fft subsampling_factor n_subsampling_channels
//                     n_conv_kernel n_pred_dim n_pred_layers n_tdt_durations n_max_tokens
inline EngineKind
detect_stt_engine_kind(const char *Path)
{
	if (Path == nullptr) return ENGINE_UNKNOWN;

	FILE *File = std::fopen(Path, "rb");
	if (File == nullptr) return ENGINE_UNKNOWN;

	uint32_t Magic = 0;
	int32_t Ints[15] = {};
	bool Ok = std::fread(&Magic, sizeof(Magic), 1, File) == 1;
	Ok = Ok && std::fread(Ints, sizeof(Ints), 1, File) == 1;
	std::fclose(File);
	if (!Ok) return ENGINE_UNKNOWN;

	if (Magic != 0x67676d6c) return ENGINE_UNKNOWN;

	if (Ints[5] == 448 && Ints[0] >= 51800 && Ints[0] <= 52000)
	{
		return ENGINE_WHISPER;
	}

	if (Ints[0] == 8192 && Ints[7] == 512 && Ints[13] >= 1 && Ints[13] <= 32)
	{
		return ENGINE_PARAKEET;
	}

	return ENGINE_UNKNOWN;
}

// Returns true on success, false on failure.
// InferenceDeviceIndex: 0 = CPU, >= 1 = GPU (CUDA device = InferenceDeviceIndex - 1)
inline bool
load_stt_model(WhisperModelState *State, const char *ModelPath,
	int ModelIndex, int InferenceDeviceIndex)
{
	if (State->IsLoaded)
	{
		if (State->Kind == ENGINE_PARAKEET)
		{
			parakeet_free(State->ParakeetContext);
			State->ParakeetContext = nullptr;
		}
		else
		{
			whisper_free(State->WhisperContext);
			State->WhisperContext = nullptr;
		}
		State->IsLoaded = false;
	}

	bool UseGpu = (InferenceDeviceIndex > 0);
	int GpuDevice = UseGpu ? (InferenceDeviceIndex - 1) : 0;

	if (UseGpu)
	{
		ggml_backend_dev_t GpuDev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
		if (GpuDev == nullptr)
		{
			UseGpu = false;
			GpuDevice = 0;
		}
	}

	State->Kind = detect_stt_engine_kind(ModelPath);

	if (State->Kind == ENGINE_UNKNOWN) return false;

	if (State->Kind == ENGINE_PARAKEET)
	{
		parakeet_context_params ContextParams = parakeet_context_default_params();
		ContextParams.use_gpu = UseGpu;
		ContextParams.gpu_device = GpuDevice;

		try
		{
			State->ParakeetContext = parakeet_init_from_file_with_params(ModelPath, ContextParams);
		}
		catch (const std::exception &)
		{
			State->ParakeetContext = nullptr;
		}

		if (State->ParakeetContext == nullptr) return false;
	}
	else
	{
		whisper_context_params ContextParams = whisper_context_default_params();
		ContextParams.use_gpu = UseGpu;
		ContextParams.flash_attn = UseGpu;
		ContextParams.gpu_device = GpuDevice;

		// whisper_init_from_file_with_params can throw std::runtime_error (e.g. via
		// whisper_backend_init when the CPU backend plugin is missing/broken). It
		// runs on the model-transition worker thread, where an uncaught exception
		// would call std::terminate — catch it and surface a graceful load failure.
		try
		{
			State->WhisperContext = whisper_init_from_file_with_params(ModelPath, ContextParams);
		}
		catch (const std::exception &)
		{
			State->WhisperContext = nullptr;
		}

		if (State->WhisperContext == nullptr) return false;
	}

	State->IsLoaded = true;
	State->LoadedModelIndex = ModelIndex;
	State->LoadedInferenceDeviceIndex = InferenceDeviceIndex;
	State->ModelPath = ModelPath;

	return true;
}

inline void
unload_stt_model(WhisperModelState *State)
{
	if (!State->IsLoaded) return;

	if (State->Kind == ENGINE_PARAKEET)
	{
		if (State->ParakeetContext == nullptr) return;

		parakeet_free(State->ParakeetContext);
		State->ParakeetContext = nullptr;
	}
	else
	{
		if (State->WhisperContext == nullptr) return;

		whisper_free(State->WhisperContext);
		State->WhisperContext = nullptr;
	}

	State->IsLoaded = false;
	State->LoadedModelIndex = -1;
	State->LoadedInferenceDeviceIndex = -1;
	State->ModelPath = "";
}

inline bool
is_stt_model_loaded(WhisperModelState *State)
{
	if (!State->IsLoaded) return false;
	if (State->Kind == ENGINE_PARAKEET) return State->ParakeetContext != nullptr;
	return State->WhisperContext != nullptr;
}
