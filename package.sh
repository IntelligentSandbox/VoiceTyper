#!/bin/bash

set -e

VERSION=$(cat VERSION | tr -d '[:space:]')
DIST_DIR="dist"

rm -rf build/Release
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# --- CUDA build ---
echo ""
echo "=== Building Release (CUDA) ==="
./build_git_bash.sh cuda

CUDA_ZIP="$DIST_DIR/voicetyper-v${VERSION}-cuda.zip"
CUDA_MSI="$DIST_DIR/voicetyper-v${VERSION}-cuda.msi"

echo ""
echo "=== Creating $CUDA_ZIP ==="
cd build/Release && 7z a -tzip "../../$CUDA_ZIP" -r . > /dev/null && cd ../..

echo ""
echo "=== Building $CUDA_MSI ==="
mkdir -p build/Release/data
echo '{}' > build/Release/data/settings.json
wix build -o "$CUDA_MSI" -pdbtype none -d "BuildOutput=$(pwd)/build/Release" -d "ProductVersion=$VERSION" packaging/VoiceTyper.wxs

echo ""
echo "=== Cleaning build/Release ==="
rm -rf build/Release

# --- CPU build ---
echo ""
echo "=== Building Release (CPU) ==="
./build_git_bash.sh

CPU_ZIP="$DIST_DIR/voicetyper-v${VERSION}-cpu.zip"
CPU_MSI="$DIST_DIR/voicetyper-v${VERSION}-cpu.msi"

echo ""
echo "=== Creating $CPU_ZIP ==="
cd build/Release && 7z a -tzip "../../$CPU_ZIP" -r . > /dev/null && cd ../..

echo ""
echo "=== Building $CPU_MSI ==="
mkdir -p build/Release/data
echo '{}' > build/Release/data/settings.json
wix build -o "$CPU_MSI" -d "BuildOutput=$(pwd)/build/Release" -d "ProductVersion=$VERSION" packaging/VoiceTyper.wxs

echo ""
echo "=== Done ==="
echo "Created:"
echo "  $CUDA_ZIP"
echo "  $CUDA_MSI"
echo "  $CPU_ZIP"
echo "  $CPU_MSI"
