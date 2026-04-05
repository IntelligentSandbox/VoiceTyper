#!/bin/bash

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

EXTRA_FLAGS=("-DVOICETYPER_CUDA=$USE_CUDA")
if [ "$USE_CUDA" = "ON" ]; then
	export CUDA_PATH="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
	EXTRA_FLAGS+=("-DCUDAToolkit_ROOT=$CUDA_PATH")
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ../.. -G "Visual Studio 17 2022" -A x64 "${EXTRA_FLAGS[@]}"
cmake --build . --config "$BUILD_TYPE"
cd ../..

cp -r stt_models "$OUTPUT_DIR/"
cp -r vad_models "$OUTPUT_DIR/"
cp -r media "$OUTPUT_DIR/"
mkdir -p "$OUTPUT_DIR/data"
echo "{}" > "$OUTPUT_DIR/data/settings.json"

if [ "$USE_CUDA" = "ON" ]; then
	CUDA_DLL_DIR="$CUDA_PATH/bin"
	if [ -d "$CUDA_PATH/bin/x64" ]; then
		CUDA_DLL_DIR="$CUDA_PATH/bin/x64"
	fi
	cp -u "$CUDA_DLL_DIR"/cublas64_*.dll "$OUTPUT_DIR/"
	cp -u "$CUDA_DLL_DIR"/cublasLt64_*.dll "$OUTPUT_DIR/"
fi
