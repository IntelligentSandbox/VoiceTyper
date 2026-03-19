#!/bin/bash

BUILD_TYPE="Release"
for arg in "$@"; do
	case "$arg" in
		debug) BUILD_TYPE="Debug" ;;
	esac
done

./build/$BUILD_TYPE/VoiceTyper.exe
