#!/bin/bash

set -e

USE_CUDA=OFF
OUTPUT_PATH=""

for arg in "$@"; do
	case "$arg" in
		cuda) USE_CUDA=ON ;;
		--output=*) OUTPUT_PATH="${arg#--output=}" ;;
	esac
done

VERSION=$(cat VERSION | tr -d '[:space:]')

if [ "$USE_CUDA" = "ON" ]; then
	VARIANT="cuda"
else
	VARIANT="cpu"
fi

BUILD_OUTPUT="build/Release_${VARIANT}"
DIST_DIR="dist"
MSI_OUTPUT="$DIST_DIR/voicetyper-v${VERSION}-${VARIANT}.msi"

if [ -n "$OUTPUT_PATH" ]; then
	MSI_OUTPUT="$OUTPUT_PATH"
fi

if [ ! -d "$BUILD_OUTPUT" ]; then
	echo "Error: build output directory '$BUILD_OUTPUT' does not exist."
	echo "Run './build_git_bash.sh${USE_CUDA:+ cuda}' first."
	exit 1
fi

mkdir -p "$DIST_DIR"
mkdir -p "$(dirname "$MSI_OUTPUT")"

echo ""
echo "=== Building $MSI_OUTPUT ==="
wix build -o "$MSI_OUTPUT" -pdbtype none \
	-d "BuildOutput=$(pwd)/$BUILD_OUTPUT" \
	-d "ProductVersion=$VERSION" \
	packaging/VoiceTyper.wxs

echo ""
echo "=== Done ==="
echo "MSI created at: $MSI_OUTPUT"
