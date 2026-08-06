#!/bin/bash

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_TYPE="Release"
BUILD_CUDA_PLUGIN=0

for arg in "$@"; do
	case "$arg" in
		debug) BUILD_TYPE="Debug" ;;
		cuda) BUILD_CUDA_PLUGIN=1 ;;
	esac
done

BUILD_JOBS="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"
CUDA_PATH="${VOICETYPER_CUDA_PATH:-C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2}"

sync_asset_dir() {
	local source_dir="$1"
	local output_dir="$2"

	mkdir -p "$output_dir"

	if command -v rsync >/dev/null 2>&1; then
		rsync -a --delete "$source_dir/" "$output_dir/"
		return
	fi

	cp -ru "$source_dir/." "$output_dir/"
}

cd "$ROOT_DIR" || exit 1

CPU_BUILD_DIR="build/cpu"
CPU_OUTPUT_DIR="build/${BUILD_TYPE}_cpu"

# Base CPU build — always runs. Produces VoiceTyper.exe + whisper.dll +
# ggml.dll (+ ggml-base.dll + ggml-cpu.dll) in CPU_OUTPUT_DIR. No CUDA toolkit
# required.
cmake -S . -B "$CPU_BUILD_DIR" -G "Visual Studio 17 2022" -A x64 \
	-DVOICETYPER_BUILD_CUDA_PLUGIN=OFF
cmake --build "$CPU_BUILD_DIR" --config "$BUILD_TYPE" --parallel "$BUILD_JOBS"

sync_asset_dir stt_models "$CPU_OUTPUT_DIR/stt_models"
sync_asset_dir vad_models "$CPU_OUTPUT_DIR/vad_models"
cp -u media/voicetyper-icon.ico "$CPU_OUTPUT_DIR/app.ico"
touch "$CPU_OUTPUT_DIR/settings.ini"
rm -rf "$CPU_OUTPUT_DIR/media" "$CPU_OUTPUT_DIR/data"
rm -f "$CPU_OUTPUT_DIR/VoiceTyperBench.exe" "$CPU_OUTPUT_DIR/voicetyper-icon.ico" "$CPU_OUTPUT_DIR/voicetyper-icon.png"

# Optional CUDA plugin build — only when `cuda` is passed. Configures a
# separate build dir with VOICETYPER_BUILD_CUDA_PLUGIN=ON and builds ONLY the
# ggml-cuda MODULE target. The result is assembled into CUDA_OUTPUT_DIR as a
# copy of the CPU base plus a self-contained cuda/ drop-in folder.
if [ "$BUILD_CUDA_PLUGIN" = "1" ]; then
	CUDA_PLUGIN_BUILD_DIR="build/cuda-plugin"
	CUDA_PLUGIN_OUTPUT_DIR="build/${BUILD_TYPE}_cuda-plugin"
	CUDA_OUTPUT_DIR="build/${BUILD_TYPE}_cuda"

	cmake -S . -B "$CUDA_PLUGIN_BUILD_DIR" -G "Visual Studio 17 2022" -A x64 \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=ON \
		-DCUDAToolkit_ROOT="$CUDA_PATH"
	cmake --build "$CUDA_PLUGIN_BUILD_DIR" --config "$BUILD_TYPE" --target ggml-cuda --parallel "$BUILD_JOBS"

	rm -rf "$CUDA_OUTPUT_DIR"
	cp -a "$CPU_OUTPUT_DIR" "$CUDA_OUTPUT_DIR"
	mkdir -p "$CUDA_OUTPUT_DIR/cuda"

	# ggml-cuda.dll lands in the plugin build's per-target output dir; fall back
	# to the subproject's default bin/ tree if the per-target dir wasn't honored.
	GGML_CUDA_DLL=""
	for candidate in \
		"$CUDA_PLUGIN_OUTPUT_DIR/ggml-cuda.dll" \
		"$CUDA_PLUGIN_BUILD_DIR/bin/$BUILD_TYPE/ggml-cuda.dll" \
		"$CUDA_PLUGIN_BUILD_DIR/bin/ggml-cuda.dll"; do
		if [ -f "$candidate" ]; then
			GGML_CUDA_DLL="$candidate"
			break
		fi
	done
	if [ -z "$GGML_CUDA_DLL" ]; then
		echo "Error: ggml-cuda.dll not found in cuda-plugin build output." >&2
		exit 1
	fi
	cp -u "$GGML_CUDA_DLL" "$CUDA_OUTPUT_DIR/cuda/"

	# CUDA runtime DLLs that ggml-cuda.dll imports. The Windows loader searches
	# the plugin's own folder for its dependents, so cuda/ is self-contained.
	CUDA_DLL_DIR="$CUDA_PATH/bin"
	if [ -d "$CUDA_PATH/bin/x64" ]; then
		CUDA_DLL_DIR="$CUDA_PATH/bin/x64"
	fi
	cp -u "$CUDA_DLL_DIR"/cublas64_*.dll "$CUDA_OUTPUT_DIR/cuda/"
	cp -u "$CUDA_DLL_DIR"/cublasLt64_*.dll "$CUDA_OUTPUT_DIR/cuda/"
	cp -u "$CUDA_DLL_DIR"/cudart64_*.dll "$CUDA_OUTPUT_DIR/cuda/"
fi
