#!/bin/bash

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_TYPE="Release"
USE_CUDA=OFF

for arg in "$@"; do
	case "$arg" in
		debug) BUILD_TYPE="Debug" ;;
		cuda) USE_CUDA=ON ;;
	esac
done

if [ "$USE_CUDA" = "ON" ]; then
	BUILD_DIR="build/cuda"
	VARIANT="cuda"
else
	BUILD_DIR="build/cpu"
	VARIANT="cpu"
fi

OUTPUT_DIR="build/${BUILD_TYPE}_${VARIANT}"
BUILD_JOBS="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"

EXTRA_FLAGS=("-DVOICETYPER_CUDA=$USE_CUDA")
if [ "$USE_CUDA" = "ON" ]; then
	export CUDA_PATH="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
	EXTRA_FLAGS+=("-DCUDAToolkit_ROOT=$CUDA_PATH")
fi

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

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ../.. -G "Visual Studio 17 2022" -A x64 "${EXTRA_FLAGS[@]}"
cmake --build . --config "$BUILD_TYPE" --parallel "$BUILD_JOBS"
cd ../..

sync_asset_dir stt_models "$OUTPUT_DIR/stt_models"
sync_asset_dir vad_models "$OUTPUT_DIR/vad_models"
sync_asset_dir media "$OUTPUT_DIR/media"
mkdir -p "$OUTPUT_DIR/data"
touch "$OUTPUT_DIR/data/settings.ini"

if [ "$USE_CUDA" = "ON" ]; then
	CUDA_DLL_DIR="$CUDA_PATH/bin"
	if [ -d "$CUDA_PATH/bin/x64" ]; then
		CUDA_DLL_DIR="$CUDA_PATH/bin/x64"
	fi
	cp -u "$CUDA_DLL_DIR"/cublas64_*.dll "$OUTPUT_DIR/"
	cp -u "$CUDA_DLL_DIR"/cublasLt64_*.dll "$OUTPUT_DIR/"
fi
