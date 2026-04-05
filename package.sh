#!/bin/bash

set -e

VERSION=$(cat VERSION | tr -d '[:space:]')
DIST_DIR="dist"
TOTAL_START=$SECONDS

rm -rf build/Release_cuda
rm -rf build/Release_cpu
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# --- CUDA build ---
echo ""
echo "=== Building Release (CUDA) ==="
START=$SECONDS
./build_git_bash.sh cuda
echo "    Build took $((SECONDS - START))s"

CUDA_ZIP="$DIST_DIR/voicetyper-v${VERSION}-cuda.zip"

echo ""
echo "=== Creating $CUDA_ZIP ==="
START=$SECONDS
cd build/Release_cuda && 7z a -tzip "../../$CUDA_ZIP" -r . > /dev/null && cd ../..
echo "    Zip took $((SECONDS - START))s"

START=$SECONDS
./build_msi.sh cuda
echo "    MSI took $((SECONDS - START))s"

echo ""
echo "=== Cleaning build/Release_cuda ==="
rm -rf build/Release_cuda

# --- CPU build ---
echo ""
echo "=== Building Release (CPU) ==="
START=$SECONDS
./build_git_bash.sh
echo "    Build took $((SECONDS - START))s"

CPU_ZIP="$DIST_DIR/voicetyper-v${VERSION}-cpu.zip"

echo ""
echo "=== Creating $CPU_ZIP ==="
START=$SECONDS
cd build/Release_cpu && 7z a -tzip "../../$CPU_ZIP" -r . > /dev/null && cd ../..
echo "    Zip took $((SECONDS - START))s"

START=$SECONDS
./build_msi.sh
echo "    MSI took $((SECONDS - START))s"

echo ""
echo "=== Done in $((SECONDS - TOTAL_START))s ==="
echo "Created:"
ls -1 "$DIST_DIR"
