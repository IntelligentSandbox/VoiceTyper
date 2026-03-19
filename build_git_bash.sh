#!/bin/bash

export CMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
BUILD_TYPE="Release"
USE_CUDA=OFF

for arg in "$@"; do
	case "$arg" in
		debug) BUILD_TYPE="Debug" ;;
		cuda) USE_CUDA=ON ;;
	esac
done

EXTRA_FLAGS=("-DVOICETYPER_CUDA=$USE_CUDA")
if [ "$USE_CUDA" = "ON" ]; then
	export CUDA_PATH="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
	EXTRA_FLAGS+=("-DCUDAToolkit_ROOT=$CUDA_PATH")
fi

mkdir -p build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 "${EXTRA_FLAGS[@]}"
cmake --build . --config "$BUILD_TYPE"
cd ..
