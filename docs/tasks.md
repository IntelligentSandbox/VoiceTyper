# Tasks

Tasks are worked top to bottom. Each task is committed individually.

## NVIDIA CUDA GPU Support

- [x] Enable GGML_CUDA in the build system
      In `CMakeLists.txt`, add an option `VOICETYPER_CUDA` (default OFF). When ON, set `GGML_CUDA ON` instead of OFF.
      Update `build_git_bash.sh` to accept a `cuda` flag (e.g. `./build_git_bash.sh cuda` or `./build_git_bash.sh debug cuda`)
      that passes `-DVOICETYPER_CUDA=ON` to cmake.
      Add a post-build step to copy CUDA runtime DLLs (cublas, cublasLt, cudart) to the output directory when CUDA is enabled.

- [x] Add CUDA device discovery to query_inference_devices
      In `system.h` `query_inference_devices()`, when the build has CUDA support (`GGML_CUDA` is defined or
      linked), call `ggml_backend_cuda_get_device_count()` and `ggml_backend_cuda_get_device_description()`
      to enumerate available NVIDIA GPUs. Add each as an entry like "GPU: NVIDIA RTX 4090" to
      `AppState->InferenceDevices`. Guard this behind `#ifdef GGML_USE_CUDA` (or the appropriate whisper.cpp
      preprocessor define for CUDA availability). Remove the existing TODO comment.

- [x] Wire GPU selection through to model loading
      In `whisper_wrapper.h` `load_whisper_model()`, accept the current inference device index and device list.
      If the selected device is a GPU entry (index > 0 when GPUs are present), set `ContextParams.use_gpu = true`
      and `ContextParams.gpu_device` to the appropriate CUDA device index. Otherwise keep `use_gpu = false`.
      Update all call sites of `load_whisper_model()` to pass the device info.

- [x] Reload model when inference device changes
      In `control.h` `update_inference_device_selection()`, after updating the index, if a model is currently
      loaded (`WhisperState.IsLoaded`), unload it and reload it so it picks up the new device setting.
      This handles the case where a user switches from CPU to GPU (or vice versa) while a model is loaded.

- [x] Persist and restore inference device setting
      Using the existing settings infrastructure in `platform_win32.h`, save and load the selected inference
      device preference (store the device name string, not the index, since indices may change across restarts).
      On startup, match the saved device name against discovered devices and restore the selection.
