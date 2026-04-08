#!/bin/bash

set -e

VERSION=$(cat VERSION | tr -d '[:space:]')
DIST_DIR="dist"
TOTAL_START=$SECONDS
CPU_BUILD="build/Release_cpu"
CUDA_BUILD="build/Release_cuda"
CPU_MODELS_DIR="$CPU_BUILD/stt_models"
CUDA_MODELS_DIR="$CUDA_BUILD/stt_models"
CPU_VAD_DIR="$CPU_BUILD/vad_models"
CUDA_VAD_DIR="$CUDA_BUILD/vad_models"
CPU_EXE_DIST="$DIST_DIR/VoiceTyper.exe"
CPU_MODELS_ZIP="$DIST_DIR/voicetyper-v${VERSION}-cpu-base-en-silero.zip"
CUDA_NO_MODELS_ZIP="$DIST_DIR/voicetyper-v${VERSION}-cuda-no-models.zip"
CUDA_MODELS_ZIP="$DIST_DIR/voicetyper-v${VERSION}-cuda-base-en-silero.zip"
CPU_MSI="$DIST_DIR/voicetyper-v${VERSION}-cpu.msi"
CUDA_MSI="$DIST_DIR/voicetyper-v${VERSION}-cuda.msi"

keep_only_base_en_and_silero() {
	local build_output="$1"

	rm -rf "$build_output/stt_models"
	mkdir -p "$build_output/stt_models"
	cp stt_models/ggml-base.en.bin "$build_output/stt_models/"

	rm -rf "$build_output/vad_models"
	mkdir -p "$build_output/vad_models"
	cp vad_models/ggml-silero-v5.1.2.bin "$build_output/vad_models/"
}

remove_models() {
	local build_output="$1"
	rm -rf "$build_output/stt_models" "$build_output/vad_models"
}

zip_dir() {
	local source_dir="$1"
	local output_zip="$2"
	local absolute_output_zip

	absolute_output_zip="$(pwd)/$output_zip"
	(cd "$source_dir" && 7z a -tzip "$absolute_output_zip" -r . > /dev/null)
}

rm -rf "$CUDA_BUILD"
rm -rf "$CPU_BUILD"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# --- CUDA build ---
echo ""
echo "=== Building Release (CUDA) ==="
START=$SECONDS
./build.sh cuda
echo "    Build took $((SECONDS - START))s"

echo ""
echo "=== Creating $CUDA_NO_MODELS_ZIP ==="
START=$SECONDS
remove_models "$CUDA_BUILD"
zip_dir "$CUDA_BUILD" "$CUDA_NO_MODELS_ZIP"
echo "    Zip took $((SECONDS - START))s"

echo ""
echo "=== Creating CUDA base.en + silero distribution ==="
START=$SECONDS
keep_only_base_en_and_silero "$CUDA_BUILD"
zip_dir "$CUDA_BUILD" "$CUDA_MODELS_ZIP"
echo "    Zip took $((SECONDS - START))s"

START=$SECONDS
./build_msi.sh cuda --output="$CUDA_MSI"
echo "    MSI took $((SECONDS - START))s"

# --- CPU build ---
echo ""
echo "=== Building Release (CPU) ==="
START=$SECONDS
./build.sh
echo "    Build took $((SECONDS - START))s"

echo ""
echo "=== Copying CPU exe ==="
START=$SECONDS
cp "$CPU_BUILD/VoiceTyper.exe" "$CPU_EXE_DIST"
echo "    Copy took $((SECONDS - START))s"

echo ""
echo "=== Creating CPU base.en + silero distribution ==="
START=$SECONDS
keep_only_base_en_and_silero "$CPU_BUILD"
zip_dir "$CPU_BUILD" "$CPU_MODELS_ZIP"
echo "    Zip took $((SECONDS - START))s"

START=$SECONDS
./build_msi.sh --output="$CPU_MSI"
echo "    MSI took $((SECONDS - START))s"

echo ""
echo "=== Done in $((SECONDS - TOTAL_START))s ==="
echo "Created:"
ls -1 "$DIST_DIR"
