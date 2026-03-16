#!/bin/bash

export CMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
BUILD_TYPE="Release"
CUDA_FLAG=""

for arg in "$@"; do
	case "$arg" in
		debug) BUILD_TYPE="Debug" ;;
		cuda)  CUDA_FLAG="-DVOICETYPER_CUDA=ON" ;;
	esac
done

mkdir -p build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 $CUDA_FLAG
cmake --build . --config "$BUILD_TYPE"
cd ..
