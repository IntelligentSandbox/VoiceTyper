#!/bin/bash

export CMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
BUILD_TYPE="Release"
USE_CUDA=OFF
USE_IMGUI=OFF

for arg in "$@"; do
	case "$arg" in
		debug) BUILD_TYPE="Debug" ;;
		cuda) USE_CUDA=ON ;;
		imgui) USE_IMGUI=ON ;;
	esac
done

BUILD_DIR="build"
if [ "$USE_IMGUI" = "ON" ]; then
	BUILD_DIR="build-imgui"
fi

EXTRA_FLAGS=("-DVOICETYPER_CUDA=$USE_CUDA" "-DVOICETYPER_USE_IMGUI=$USE_IMGUI")
if [ "$USE_CUDA" = "ON" ]; then
	export CUDA_PATH="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
	EXTRA_FLAGS+=("-DCUDAToolkit_ROOT=$CUDA_PATH")
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -G "Visual Studio 17 2022" -A x64 "${EXTRA_FLAGS[@]}"
cmake --build . --config "$BUILD_TYPE"
cd ..

if [ "$USE_CUDA" = "ON" ]; then
	CUDA_DLL_DIR="$CUDA_PATH/bin"
	if [ -d "$CUDA_PATH/bin/x64" ]; then
		CUDA_DLL_DIR="$CUDA_PATH/bin/x64"
	fi
	cp -u "$CUDA_DLL_DIR"/cublas64_*.dll "$BUILD_DIR/$BUILD_TYPE/"
	cp -u "$CUDA_DLL_DIR"/cublasLt64_*.dll "$BUILD_DIR/$BUILD_TYPE/"
fi
