#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

VERSION="$(tr -d '[:space:]' < VERSION)"
DIST_DIR="dist"
PLATFORM="x86_64-linux"
TOTAL_START=$SECONDS

VAD_MODEL="vad_models/ggml-silero-v5.1.2.bin"

# Use ccache for the CUDA targets when the shared cache dir exists, so the
# cuda-static / cuda-appimage packages reuse already-compiled kernels instead
# of rebuilding nvcc output from scratch. No-op on machines without the cache.
CCACHE_FLAG=""
if [ -d "${CCACHE_DIR:-/var/cache/voicetyper-ccache}" ]; then
	CCACHE_FLAG="--ccache"
fi

# Verify the VAD model file exists before we start building. STT models are
# fetched on demand by the in-app downloader (huggingface.co/ggerganov/whisper.cpp).
if [ ! -f "$VAD_MODEL" ]; then
	echo "Error: VAD model file '$VAD_MODEL' not found." >&2
	exit 1
fi

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# ─── Helpers ─────────────────────────────────────────────────────────────

build_target() {
	local package="$1"
	local out_link="$2"
	local ccache=""
	case "$package" in cuda-*) ccache="$CCACHE_FLAG" ;; esac
	bash tools/nix-build.sh $ccache "$package" -- --out-link "$out_link"
}

# Stage a static binary (CPU or CUDA) into a clean directory layout and tar it.
# STT models are no longer bundled — users download them via the in-app
# downloader on first run. The VAD model is bundled because it is small and
# required for streaming/record silence detection.
#
# CPU layout (truly static, no dynamic deps):
#   VoiceTyper-v...-cpu-static/
#     VoiceTyper
#     vad_models/ggml-silero-v5.1.2.bin
#
# CUDA layout (self-contained bundle, see flake.nix cuda-static):
#   VoiceTyper-v...-cuda-static/
#     bin/
#       VoiceTyper        # shell wrapper invoking lib/ld-linux + libexec/...
#       VoiceTyperBench
#     libexec/
#       VoiceTyper        # actual ELF binary (rpath $ORIGIN/../lib)
#       VoiceTyperBench
#     lib/                # full transitive .so closure + ld-linux + CUDA runtime
#     vad_models/ggml-silero-v5.1.2.bin
package_static() {
	local result_link="$1"
	local variant="$2"
	local name="VoiceTyper-v${VERSION}-${PLATFORM}-${variant}-static.tar.gz"
	local top="${name%.tar.gz}"
	local stage="build/stage_${PLATFORM}_${variant}_static"
	local root="$stage/$top"

	if [ -d "$stage" ]; then
		chmod -R u+w "$stage"
	fi
	rm -rf "$stage"
	mkdir -p "$root/vad_models"

	if [ "$variant" = "cuda" ]; then
		cp -r "$result_link/bin" "$root/bin"
		cp -r "$result_link/libexec" "$root/libexec"
		cp -r "$result_link/lib" "$root/lib"
		chmod -R u+w "$root"
	else
		cp "$result_link/bin/VoiceTyper" "$root/VoiceTyper"
		chmod +w "$root/VoiceTyper"
	fi

	cp "$VAD_MODEL" "$root/vad_models/"

	tar -C "$stage" -czf "$DIST_DIR/$name" "$top"
	echo "    packaged $name"
}

# Find the AppImage file inside a nix result symlink.
find_appimage() {
	local out_link="$1"
	local pattern="$2"
	local src
	src="$(find -L "$out_link" -name "$pattern" -type f | head -n 1)"
	if [ -z "$src" ]; then
		echo "Error: no artifact matching '$pattern' under $out_link." >&2
		exit 1
	fi
	echo "$src"
}

# Inject the VAD model into an AppImage by extracting the squashfs, adding the
# file into usr/share/voicetyper/, rewriting AppRun, and repackaging.
bundle_appimage_vad() {
	local input="$1"
	local output_name="$2"
	local output="$DIST_DIR/$output_name"
	local work="build/appimage_repack"

	# Parse the ELF header to find where the squashfs payload begins.
	local e_shoff e_shentsize e_shnum elf_size
	e_shoff=$(od -A n -t u8 --endian=little -j 40 -N 8 "$input" | tr -d ' ')
	e_shentsize=$(od -A n -t u2 --endian=little -j 58 -N 2 "$input" | tr -d ' ')
	e_shnum=$(od -A n -t u2 --endian=little -j 60 -N 2 "$input" | tr -d ' ')
	elf_size=$((e_shoff + e_shentsize * e_shnum))

	rm -rf "$work"
	mkdir -p "$work"

	# Split the AppImage into ELF runtime + squashfs payload.
	head -c "$elf_size" "$input" > "$work/runtime"
	chmod +x "$work/runtime"
	tail -c "+$((elf_size + 1))" "$input" > "$work/payload.squashfs"

	# Unpack the squashfs.
	if command -v unsquashfs >/dev/null 2>&1; then
		unsquashfs -no-progress -d "$work/squashfs-root" "$work/payload.squashfs" > /dev/null
	else
		local store_path
		store_path=$(nix build "nixpkgs#squashfsTools" --no-link --print-out-paths 2>/dev/null) || \
			store_path=$(nix-build '<nixpkgs>' -A squashfsTools --no-out-link 2>/dev/null)
		if [ -z "$store_path" ]; then
			echo "Error: could not find unsquashfs via PATH or nix." >&2
			exit 1
		fi
		"$store_path/bin/unsquashfs" -no-progress -d "$work/squashfs-root" "$work/payload.squashfs" > /dev/null
	fi

	# Inject VAD model into the AppDir.
	local sq="$work/squashfs-root"
	mkdir -p "$sq/usr/share/voicetyper/vad_models"
	cp "$VAD_MODEL" "$sq/usr/share/voicetyper/vad_models/"

	# Rewrite AppRun to point VOICETYPER_DATA_DIR at the bundled VAD model.
	printf '%s\n' \
		'#!/bin/bash' \
		'dir="$(dirname "$(readlink -f "$0")")"' \
		'export VOICETYPER_DATA_DIR="$dir/usr/share/voicetyper"' \
		'exec "$dir/usr/bin/VoiceTyper" "$@"' \
		> "$sq/AppRun"
	chmod +x "$sq/AppRun"

	# Rebuild squashfs and reassemble the AppImage.
	if command -v mksquashfs >/dev/null 2>&1; then
		mksquashfs "$sq" "$work/new-payload.squashfs" -comp xz -noappend -root-owned -no-progress > /dev/null
	else
		local store_path
		store_path=$(nix build "nixpkgs#squashfsTools" --no-link --print-out-paths 2>/dev/null) || \
			store_path=$(nix-build '<nixpkgs>' -A squashfsTools --no-out-link 2>/dev/null)
		if [ -z "$store_path" ]; then
			echo "Error: could not find mksquashfs via PATH or nix." >&2
			exit 1
		fi
		"$store_path/bin/mksquashfs" "$sq" "$work/new-payload.squashfs" -comp xz -noappend -root-owned -no-progress > /dev/null
	fi
	cat "$work/runtime" "$work/new-payload.squashfs" > "$output"
	chmod +w "$output"

	rm -rf "$work"
	echo "    packaged $output_name"
}

# ─── Build & package ────────────────────────────────────────────────────

echo "=== Building portable Linux packages ($PLATFORM) for v$VERSION ==="

echo "--- appimage (cpu) ---"
START=$SECONDS
build_target appimage "result-appimage"
echo "    build took $((SECONDS - START))s"
CPU_APPIMAGE=$(find_appimage "result-appimage" "VoiceTyper-*-x86_64.AppImage")
cp "$CPU_APPIMAGE" "$DIST_DIR/VoiceTyper-v${VERSION}-${PLATFORM}-cpu-appimage.AppImage"
bundle_appimage_vad "$CPU_APPIMAGE" "VoiceTyper-v${VERSION}-${PLATFORM}-cpu-appimage.AppImage"

echo "--- static (cpu) ---"
START=$SECONDS
build_target static "result-static"
echo "    build took $((SECONDS - START))s"
package_static "result-static" "cpu"

echo "--- appimage (cuda) ---"
START=$SECONDS
build_target cuda-appimage "result-cuda-appimage"
echo "    build took $((SECONDS - START))s"
CUDA_APPIMAGE=$(find_appimage "result-cuda-appimage" "VoiceTyper-*-x86_64-cuda.AppImage")
cp "$CUDA_APPIMAGE" "$DIST_DIR/VoiceTyper-v${VERSION}-${PLATFORM}-cuda-appimage.AppImage"
bundle_appimage_vad "$CUDA_APPIMAGE" "VoiceTyper-v${VERSION}-${PLATFORM}-cuda-appimage.AppImage"

echo "--- static (cuda) ---"
START=$SECONDS
build_target cuda-static "result-cuda-static"
echo "    build took $((SECONDS - START))s"
package_static "result-cuda-static" "cuda"

echo ""
echo "=== Done in $((SECONDS - TOTAL_START))s ==="
echo "Created:"
ls -1 "$DIST_DIR"
