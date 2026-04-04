#pragma once

#include "whisper.h"
#include <string>

#ifdef DEBUG
	#include <cstdio>
#endif

#ifndef DEBUG
static void whisper_log_suppress(ggml_log_level, const char *, void *) {}
#endif

struct WhisperModelState
{
	whisper_context *Context;
	bool IsLoaded;
	int LoadedModelIndex;
	std::string ModelPath;
};

inline
void
init_whisper_state(WhisperModelState *State)
{
	State->Context = nullptr;
	State->IsLoaded = false;
	State->LoadedModelIndex = -1;
	State->ModelPath = "";
	#ifdef DEBUG
		printf("[whisper_wrapper] Whisper state initialized\n");
	#else
		whisper_log_set(whisper_log_suppress, nullptr);
	#endif
}

// Returns true on success, false on failure.
// InferenceDeviceIndex: 0 = CPU, >= 1 = GPU (CUDA device = InferenceDeviceIndex - 1)
inline
bool
load_whisper_model(WhisperModelState *State, const char *ModelPath,
	int ModelIndex, int InferenceDeviceIndex)
{
	#ifdef DEBUG
		printf("[control] Loading STT model from: %s\n", ModelPath);
	#endif

	if (State->IsLoaded)
	{
		#ifdef DEBUG
			printf("[whisper_wrapper] Model already loaded, unloading first\n");
		#endif
		whisper_free(State->Context);
		State->Context = nullptr;
		State->IsLoaded = false;
	}

	#ifdef DEBUG
		printf("[whisper_wrapper] Loading model from: %s\n", ModelPath);
	#endif

	bool UseGpu = (InferenceDeviceIndex > 0);
	int GpuDevice = UseGpu ? (InferenceDeviceIndex - 1) : 0;

	whisper_context_params ContextParams = whisper_context_default_params();
	ContextParams.use_gpu = UseGpu;
	ContextParams.flash_attn = UseGpu;
	ContextParams.gpu_device = GpuDevice;

	#ifdef DEBUG
		printf("[whisper_wrapper] Context params: use_gpu=%d, flash_attn=%d, gpu_device=%d\n",
				ContextParams.use_gpu,
				ContextParams.flash_attn,
				ContextParams.gpu_device);
		printf("[whisper_wrapper] Initializing whisper context (this may take a moment)...\n");
	#endif

	State->Context = whisper_init_from_file_with_params(ModelPath, ContextParams);

	if (State->Context == nullptr)
	{
		#ifdef DEBUG
			printf("[whisper_wrapper] ERROR: Failed to load model from: %s\n", ModelPath);
		#endif
		return false;
	}

	State->IsLoaded = true;
	State->LoadedModelIndex = ModelIndex;
	State->ModelPath = ModelPath;

	#ifdef DEBUG
		printf("[whisper_wrapper] Model loaded successfully!\n");
		printf("[whisper_wrapper] Whisper version: %s\n", whisper_version());
	#endif

	return true;
}

inline
void
unload_whisper_model(WhisperModelState *State)
{
	if (!State->IsLoaded || State->Context == nullptr)
	{
		#ifdef DEBUG
			printf("[whisper_wrapper] No model loaded to unload\n");
		#endif
		return;
	}

	#ifdef DEBUG
		printf("[whisper_wrapper] Unloading model: %s\n", State->ModelPath.c_str());
	#endif

	whisper_free(State->Context);
	State->Context = nullptr;
	State->IsLoaded = false;
	State->LoadedModelIndex = -1;
	State->ModelPath = "";

	#ifdef DEBUG
		printf("[whisper_wrapper] Model unloaded successfully\n");
	#endif
}

inline
bool
is_whisper_model_loaded(WhisperModelState *State)
{
	return State->IsLoaded && State->Context != nullptr;
}
