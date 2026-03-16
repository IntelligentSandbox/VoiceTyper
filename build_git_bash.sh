#!/bin/bash

export CMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
BUILD_TYPE="Release"

if [ "$1" == "debug" ]; then
    BUILD_TYPE="Debug"
fi

mkdir -p build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config "$BUILD_TYPE"
cd ..
