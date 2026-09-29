#pragma once

#include "whisper.h"
#include "parakeet.h"
#include "ggml-backend.h"
#include <exception>
#include <string>

enum EngineKind
{
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
	State->Kind = ENGINE_WHISPER;
	State->WhisperContext = nullptr;
	State->ParakeetContext = nullptr;
	State->IsLoaded = false;
	State->LoadedModelIndex = -1;
	State->LoadedInferenceDeviceIndex = -1;
	State->ModelPath = "";
}

inline EngineKind
detect_stt_engine_kind(const char *Path)
{
	(void)Path;
	return ENGINE_WHISPER;
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
