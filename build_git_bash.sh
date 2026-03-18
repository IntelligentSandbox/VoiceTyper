#!/bin/bash

export CMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
BUILD_TYPE="Release"
EXTRA_FLAGS=()

for arg in "$@"; do
	case "$arg" in
		debug) BUILD_TYPE="Debug" ;;
		cuda)
			export CUDA_PATH="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
			EXTRA_FLAGS+=("-DVOICETYPER_CUDA=ON")
			EXTRA_FLAGS+=("-DCUDAToolkit_ROOT=$CUDA_PATH")
			;;
	esac
done

mkdir -p build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 "${EXTRA_FLAGS[@]}"
cmake --build . --config "$BUILD_TYPE"
cd ..
