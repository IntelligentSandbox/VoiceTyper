#!/bin/bash

set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

REBUILD=0

for arg in "$@"; do
	case "$arg" in
		build) REBUILD=1 ;;
		*)
			echo "Usage: $0 [build]"
			exit 1
			;;
	esac
done

VERSION=$(cat VERSION | tr -d '[:space:]')
DIST_DIR="dist"
TOTAL_START=$SECONDS
PACKAGE_PLATFORM="x64_win"
CPU_BUILD="build/Release_cpu"
CUDA_BUILD="build/Release_cuda"
STAGE_DIR="build/package_${PACKAGE_PLATFORM}"
CPU_STAGE="$STAGE_DIR/cpu"
CUDA_STAGE="$STAGE_DIR/cuda"
CPU_EXE_DIST="$DIST_DIR/VoiceTyper-v${VERSION}-${PACKAGE_PLATFORM}-cpu.exe"
CPU_ZIP="$DIST_DIR/VoiceTyper-v${VERSION}-${PACKAGE_PLATFORM}-cpu.zip"
CUDA_ZIP="$DIST_DIR/VoiceTyper-v${VERSION}-${PACKAGE_PLATFORM}-cuda.zip"
CPU_MSI="$DIST_DIR/VoiceTyper-v${VERSION}-${PACKAGE_PLATFORM}-cpu.msi"
CUDA_MSI="$DIST_DIR/VoiceTyper-v${VERSION}-${PACKAGE_PLATFORM}-cuda.msi"

# STT models are no longer shipped in dist artifacts — the in-app downloader
# fetches them from huggingface.co/ggerganov/whisper.cpp on demand. The VAD
# model is small (~2 MB) and required for streaming/record silence detection,
# so it is still bundled.
remove_stt_models() {
	local build_output="$1"
	rm -rf "$build_output/stt_models"
}

copy_build_output() {
	local source_dir="$1"
	local target_dir="$2"

	rm -rf "$target_dir"
	mkdir -p "$(dirname "$target_dir")"
	cp -a "$source_dir" "$target_dir"
}

require_build_output() {
	local build_output="$1"
	local variant="$2"
	local build_command="$3"

	if [ -f "$build_output/VoiceTyper.exe" ]; then
		return
	fi

	echo "Error: $variant build output '$build_output' does not exist."
	echo "Run '$build_command' first, or run 'tools/win-package.sh build'."
	exit 1
}

zip_dir() {
	local source_dir="$1"
	local output_zip="$2"
	local absolute_output_zip

	absolute_output_zip="$(pwd)/$output_zip"
	(cd "$source_dir" && 7z a -tzip "$absolute_output_zip" -r . > /dev/null)
}

build_msi_from_dir() {
	local build_output="$1"
	local output_path="$2"

	wix build -o "$output_path" -pdbtype none \
		-d "BuildOutput=$(pwd)/$build_output" \
		-d "ProductVersion=$VERSION" \
		packaging/VoiceTyper.wxs
}

run_job() {
	local job_name="$1"
	shift

	echo "=== Starting $job_name ==="
	"$@" &
	JOB_PIDS+=("$!")
	JOB_NAMES+=("$job_name")
}

wait_for_jobs() {
	local failed=0
	local i
	local pid
	local job_name

	for i in "${!JOB_PIDS[@]}"; do
		pid="${JOB_PIDS[$i]}"
		job_name="${JOB_NAMES[$i]}"

		if wait "$pid"; then
			echo "=== Finished $job_name ==="
		else
			echo "Error: $job_name failed."
			failed=1
		fi
	done

	if [ "$failed" = "1" ]; then
		exit 1
	fi
}

if [ "$REBUILD" = "1" ]; then
	rm -rf "$CUDA_BUILD"
	rm -rf "$CPU_BUILD"
else
	require_build_output "$CUDA_BUILD" "cuda" "tools/win-build.sh cuda"
	require_build_output "$CPU_BUILD" "cpu" "tools/win-build.sh"
fi
rm -rf "$DIST_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$DIST_DIR"

if [ "$REBUILD" = "1" ]; then
	echo ""
	echo "=== Building Release (CUDA) ==="
	START=$SECONDS
	tools/win-build.sh cuda
	echo "    Build took $((SECONDS - START))s"

	echo ""
	echo "=== Building Release (CPU) ==="
	START=$SECONDS
	tools/win-build.sh
	echo "    Build took $((SECONDS - START))s"
fi

echo ""
echo "=== Staging package inputs ($PACKAGE_PLATFORM) ==="
START=$SECONDS
copy_build_output "$CUDA_BUILD" "$CUDA_STAGE"
remove_stt_models "$CUDA_STAGE"
copy_build_output "$CPU_BUILD" "$CPU_STAGE"
remove_stt_models "$CPU_STAGE"
cp "$CPU_BUILD/VoiceTyper.exe" "$CPU_EXE_DIST"
echo "    Staging took $((SECONDS - START))s"

echo ""
echo "=== Creating package artifacts ($PACKAGE_PLATFORM) ==="
START=$SECONDS
JOB_PIDS=()
JOB_NAMES=()
run_job "CUDA zip" zip_dir "$CUDA_STAGE" "$CUDA_ZIP"
run_job "CUDA MSI" build_msi_from_dir "$CUDA_STAGE" "$CUDA_MSI"
run_job "CPU zip" zip_dir "$CPU_STAGE" "$CPU_ZIP"
run_job "CPU MSI" build_msi_from_dir "$CPU_STAGE" "$CPU_MSI"
wait_for_jobs
echo "    Package artifacts took $((SECONDS - START))s"

echo ""
echo "=== Done in $((SECONDS - TOTAL_START))s ==="
echo "Created:"
ls -1 "$DIST_DIR"
