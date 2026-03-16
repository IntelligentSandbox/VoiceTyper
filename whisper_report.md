# whisper.cpp Submodule Report

## Overview

**whisper.cpp** is a high-performance C/C++ implementation of OpenAI's Whisper automatic speech recognition (ASR) model. It converts audio (16kHz PCM, processed in 30-second chunks) into text using a transformer-based encoder-decoder architecture. The project has no required external dependencies beyond the C++ standard library — GPU support is all optional.

The project is structured around two core components: **GGML** (the underlying tensor/ML library) and **whisper** (the model implementation on top of it).

---

## Status Legend

| Symbol | Meaning |
|--------|---------|
| ✅ Currently used | Voice-typer uses this now |
| 🔜 Future use | Will likely enable this later |
| ❌ Not applicable | Won't be used in this project |

---

## Core Components

### ✅ `include/whisper.h` — Public C API

The single public header exposing the entire whisper API. Key types and functions:

- `whisper_context` / `whisper_state` — model context and computation state
- `whisper_full_params` — configuration for the transcription pipeline (language, threads, beam size, etc.)
- `whisper_init_from_file_with_params()` — load a model from a `.bin` file
- `whisper_free()` — release the context
- `whisper_pcm_to_mel()` — convert raw PCM audio to log mel spectrogram
- `whisper_full()` — run the full encoder + decoder pipeline on audio
- `whisper_full_n_segments()` / `whisper_full_get_segment_text()` — retrieve transcription output
- `whisper_log_set()` — configure logging
- `whisper_version()` — get library version string

Also exposes: token-level timestamps via DTW (Dynamic Time Warping), VAD (Voice Activity Detection) integration, grammar-constrained generation (GBNF), and speaker diarization hooks.

---

### ✅ `src/` — Whisper Implementation

The core model code.

| File | Purpose |
|------|---------|
| `whisper.cpp` | ~9,000 lines. Model loading, mel spectrogram computation, encoder/decoder inference loops, quantization utilities, token processing |
| `whisper-arch.h` | Tensor name definitions for encoder/decoder/cross-attention blocks |
| `coreml/` | Apple CoreML integration — runs the encoder on Apple Neural Engine (ANE). macOS/iOS only |
| `openvino/` | Intel OpenVINO integration — alternative encoder path using OpenVINO IR format. Not used |

---

### ✅ `ggml/` — GGML Tensor Library

GGML (Generative Graph ML) is a minimalistic ML framework that whisper.cpp is built on. It handles:

- Multi-dimensional tensor operations (up to 4D)
- Computation graph construction and deferred execution
- Multiple quantization formats (Q4_0, Q5_0, Q6_K, Q8_0, etc.) to reduce model memory footprint
- Zero heap allocations at runtime once initialized
- Backend abstraction — the same graph can run on CPU, CUDA, Vulkan, etc.

Key headers: `ggml/include/ggml.h`, `ggml/include/ggml-backend.h`

---

## GGML Backends

### ✅ `ggml/src/ggml-cpu/` — CPU Backend

The default backend, always enabled. Runs inference on CPU using SIMD optimizations:

- x86: AVX2, AVX512, F16C, FMA
- ARM: NEON
- Power: VSX

Subdirectories contain specialized kernels:
- `ops.cpp` / `unary-ops.cpp` / `binary-ops.cpp` — core tensor ops
- `quants.c` — quantization/dequantization
- `vec.cpp` — vectorized operations
- `amx/` — Intel AMX (Advanced Matrix Extensions)
- `kleidiai/` — ARM optimized matrix multiply kernels
- `simd-mappings.h` — architecture-specific SIMD dispatch

This is what voice-typer uses today (CPU-only mode).

---

### 🔜 `ggml/src/ggml-cuda/` — NVIDIA CUDA Backend

GPU acceleration for NVIDIA hardware (Maxwell architecture and newer — GTX 750 Ti onwards).

Key features:
- cuBLAS integration for optimized matrix multiplication
- Custom CUDA kernels for attention, quantized matmul, and element-wise ops
- Flash Attention implementation for faster attention computation
- Multi-GPU peer memory access
- Supports all modern NVIDIA architectures up to Blackwell (RTX 5000 series)

**Windows support: Yes** — works with MSVC + CUDA Toolkit.

Enable with: `set(GGML_CUDA ON)` in CMakeLists.txt.

---

### 🔜 `ggml/src/ggml-vulkan/` — Vulkan Backend

Cross-vendor GPU acceleration via the Vulkan API. Targets AMD, NVIDIA, Intel, and Qualcomm GPUs — anything with Vulkan drivers.

Key features:
- GLSL compute shaders compiled to SPIR-V
- Cooperative matrix extensions for fast matmul (KHR, NV)
- BFloat16 support
- Validation and profiling hooks for debugging

**Windows support: Yes** — works on any system with Vulkan drivers (Radeon, GeForce, Arc, etc.). Good option if you want GPU support without requiring CUDA.

Enable with: `set(GGML_VULKAN ON)`.

---

### 🔜 `ggml/src/ggml-opencl/` — OpenCL Backend

Cross-platform GPU compute via OpenCL. More legacy than Vulkan but has broader hardware support.

Key features:
- 146+ specialized compute kernels covering all major tensor operations
- Qualcomm Adreno-specific optimizations
- Supports Q4_0, Q8_0, Q6_K quantized types
- Profiling support

**Windows support: Yes** — works with AMD, Intel, and NVIDIA OpenCL drivers.

Enable with: `set(GGML_OPENCL ON)`.

---

### 🔜 `ggml/src/ggml-blas/` — CPU BLAS Backend

Accelerates matrix operations on CPU using optimized BLAS libraries instead of (or alongside) the built-in CPU backend.

Supported libraries:
- OpenBLAS (open source, Windows-friendly)
- Intel MKL (best for Intel CPUs)
- NVIDIA NVPL
- BLIS
- Apple Accelerate (macOS only)

**Windows support: Yes.**

Enable with: `set(GGML_BLAS ON)`.

---

### 🔜 `ggml/src/ggml-sycl/` — Intel SYCL Backend

GPU acceleration for Intel hardware via Intel's oneAPI/SYCL framework. Targets:

- Intel Arc discrete GPUs (Arc 770, etc.)
- Intel Data Center Max/Flex series
- Intel integrated graphics (Meteor Lake and newer)

Written in C++17 SYCL, compiled with Intel's `icpx` or SYCL/Clang++.

**Windows support: Partial** — Windows support is still in progress upstream.

Enable with: `set(GGML_SYCL ON)`.

---

### 🔜 `ggml/src/ggml-rpc/` — RPC Backend

Allows offloading inference to a remote server over the network via sockets. Useful for:

- Running model inference on a dedicated GPU machine
- Separating heavy compute from the application process
- Multi-machine distributed inference

**Windows support: Yes** — uses standard Windows socket API.

Enable with: `set(GGML_RPC ON)`.

---

### ❌ `ggml/src/ggml-metal/` — Apple Metal Backend

GPU acceleration for Apple Silicon (M1–M4) via Apple's Metal shading language. First-class support for Apple Neural Engine (ANE) — can give 3x+ speedup on Mac.

**Windows support: No** — macOS/iOS only.

---

### ❌ `ggml/src/ggml-hip/` — AMD HIP Backend

AMD GPU acceleration via ROCm/HIP (Heterogeneous-computing Interface for Portability). Alternative to Vulkan for AMD-specific optimization.

**Windows support: Partial** — ROCm on Windows is limited. Vulkan is a better choice for AMD on Windows.

---

### ❌ `ggml/src/ggml-hexagon/` — Qualcomm Hexagon Backend

Accelerates inference on Qualcomm's Hexagon DSP and HVX (Hexagon Vector eXtensions) found in Snapdragon mobile processors. Mobile/edge devices only.

**Windows support: No.**

---

### ❌ `ggml/src/ggml-cann/` — Huawei Ascend NPU Backend

Huawei's Compute Architecture for Neural Networks. Targets Atlas 300T/300I enterprise NPU hardware.

**Windows support: No.**

---

### ❌ `ggml/src/ggml-musa/` — Moore Threads GPU Backend

Moore Threads discrete GPUs with muBLAS library. Niche Chinese GPU hardware.

**Windows support: Uncertain.**

---

### ❌ `ggml/src/ggml-zdnn/` / `ggml-zendnn/` — IBM / AMD Server Backends

- **ZDNN**: IBM z/Architecture (mainframe) neural network acceleration
- **ZendNN**: AMD EPYC server CPU optimization via AMD's ZenDNN library

**Windows support: No** — server/mainframe hardware.

---

### ❌ `ggml/src/ggml-webgpu/` — WebGPU Backend

GPU acceleration in WebAssembly/browser environments. For deploying whisper in a web app.

**Windows support: Indirect** (via browser only).

---

## Non-Source Directories

### ❌ `grammars/` — GBNF Grammar Files

Context-free grammar files (`.gbnf` format) for constraining whisper's output to specific vocabularies or formats. Three are included:

- `assistant.gbnf` — home assistant commands (lights, thermostat, etc.)
- `chess.gbnf` — chess algebraic notation
- `colors.gbnf` — color names

These are only used via the `--grammar` flag in the whisper example programs, which are not built. Not used by voice-typer.

---

### ❌ `models/` — Model Conversion & Download Scripts

Contains Python scripts for converting models from other formats to GGML `.bin`:

- `convert-pt-to-ggml.py` — from PyTorch (OpenAI Whisper)
- `convert-h5-to-ggml.py` — from HuggingFace fine-tuned models
- `convert-whisper-to-openvino.py` — generate OpenVINO IR format
- `convert-whisper-to-coreml.py` — generate CoreML models
- `convert-silero-vad-to-ggml.py` — convert VAD model

Also contains download shell scripts (`download-ggml-model.sh/.cmd`) and empty test model stubs for CI.

Voice-typer downloads pre-converted models directly — none of these scripts are needed.

Available model sizes: tiny (75 MiB) → large-v3 (2.9 GiB), with quantized (Q5_0) variants and English-only (`.en`) variants.

---

### ❌ `scripts/` — Build & Utility Scripts

Shell/Python scripts for project maintenance:

- `bench.py` / `bench-all.sh` — benchmarking across models and thread counts
- `convert-all.sh` — batch model conversion
- `sync-ggml.sh` / `sync-llama.sh` — sync library updates from upstream repos
- `quantize-all.sh` — batch quantization
- `deploy-wasm.sh` — WebAssembly deployment
- `gen-authors.sh` — contributor list generation
- `apple/` — macOS/iOS specific build helpers

None are needed for building or running voice-typer.

---

### ✅ `cmake/` — CMake Build Helpers

Support files used during the CMake build process:

- `build-info.cmake` / `git-vars.cmake` — capture version/git metadata at build time
- `whisper-config.cmake.in` — CMake package config template (allows `find_package(whisper)`)
- `whisper.pc.in` — pkg-config template
- `DefaultTargetOptions.cmake` — standard compiler/linker flag setup
- Toolchain files for cross-compilation (ARM, RISC-V, etc.)

These are required for the build to work.

---

## GPU Backend Decision Guide (Windows)

When adding GPU support to voice-typer, the recommended priority order:

1. **CUDA** — Best performance on NVIDIA hardware. Most users with a discrete GPU have NVIDIA. Well-tested, mature.
2. **Vulkan** — Best cross-vendor option (AMD + NVIDIA + Intel Arc). No vendor lock-in. Growing support.
3. **OpenCL** — Fallback for older AMD/Intel hardware without good Vulkan support.
4. **BLAS** — CPU-only but faster matrix multiply for users without a GPU.
5. **SYCL** — Intel-specific, Windows support still maturing.
6. **RPC** — Special case: useful if you want to offload inference to a separate machine.
