#pragma once

#include "whisper.h"
#include "ggml-backend.h"
#include <string>

struct WhisperModelState
{
	whisper_context *Context;
	bool IsLoaded;
	int LoadedModelIndex;
	std::string ModelPath;
};

inline void
init_whisper_state(WhisperModelState *State)
{
	State->Context = nullptr;
	State->IsLoaded = false;
	State->LoadedModelIndex = -1;
	State->ModelPath = "";
}

// Returns true on success, false on failure.
// InferenceDeviceIndex: 0 = CPU, >= 1 = GPU (CUDA device = InferenceDeviceIndex - 1)
inline bool
load_whisper_model(WhisperModelState *State, const char *ModelPath,
	int ModelIndex, int InferenceDeviceIndex)
{
	if (State->IsLoaded)
	{
		whisper_free(State->Context);
		State->Context = nullptr;
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

	whisper_context_params ContextParams = whisper_context_default_params();
	ContextParams.use_gpu = UseGpu;
	ContextParams.flash_attn = UseGpu;
	ContextParams.gpu_device = GpuDevice;

	State->Context = whisper_init_from_file_with_params(ModelPath, ContextParams);

	if (State->Context == nullptr) return false;

	State->IsLoaded = true;
	State->LoadedModelIndex = ModelIndex;
	State->ModelPath = ModelPath;

	return true;
}

inline void
unload_whisper_model(WhisperModelState *State)
{
	if (!State->IsLoaded || State->Context == nullptr) return;

	whisper_free(State->Context);
	State->Context = nullptr;
	State->IsLoaded = false;
	State->LoadedModelIndex = -1;
	State->ModelPath = "";
}

inline bool
is_whisper_model_loaded(WhisperModelState *State)
{
	return State->IsLoaded && State->Context != nullptr;
}
