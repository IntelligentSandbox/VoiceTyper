#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

VERSION="$(tr -d '[:space:]' < VERSION)"
DIST_DIR="dist"
PLATFORM="x86_64-linux"
TOTAL_START=$SECONDS

STT_MODEL="stt_models/ggml-base.en.bin"
VAD_MODEL="vad_models/ggml-silero-v5.1.2.bin"

# Use ccache for the CUDA targets when the shared cache dir exists, so the
# cuda-static / cuda-appimage packages reuse already-compiled kernels instead
# of rebuilding nvcc output from scratch. No-op on machines without the cache.
CCACHE_FLAG=""
if [ -d "${CCACHE_DIR:-/var/cache/voicetyper-ccache}" ]; then
	CCACHE_FLAG="--ccache"
fi

# Verify model files exist before we start building.
for f in "$STT_MODEL" "$VAD_MODEL"; do
	if [ ! -f "$f" ]; then
		echo "Error: model file '$f' not found." >&2
		exit 1
	fi
done

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

# Resolve a tool binary: check PATH first, fall back to nix.
get_tool() {
	local cmd="$1"
	local nix_attr="$2"
	local path
	path=$(command -v "$cmd" 2>/dev/null) && { echo "$path"; return; }
	local store_path
	store_path=$(nix build "nixpkgs#$nix_attr" --no-link --print-out-paths 2>/dev/null) || \
		store_path=$(nix-build '<nixpkgs>' -A "$nix_attr" --no-out-link 2>/dev/null)
	if [ -z "$store_path" ]; then
		echo "Error: could not resolve '$cmd' via PATH or nix." >&2
		exit 1
	fi
	echo "${store_path}/bin/${cmd}"
}

# Copy model files into a staging directory based on the variant.
#   variant: "" | "base-en" | "base-en-silero"
stage_models() {
	local dir="$1"
	local variant="$2"
	case "$variant" in
		"") ;;
		base-en)
			mkdir -p "$dir/stt_models"
			cp "$STT_MODEL" "$dir/stt_models/"
			;;
		base-en-silero)
			mkdir -p "$dir/stt_models" "$dir/vad_models"
			cp "$STT_MODEL" "$dir/stt_models/"
			cp "$VAD_MODEL" "$dir/vad_models/"
			;;
		*) echo "Error: unknown model variant '$variant'." >&2; exit 1 ;;
	esac
}

# Stage a static binary (CPU or CUDA) into a clean directory layout and tar it.
# The tarball extracts to a single top-level directory whose name matches the
# tarball stem (e.g. VoiceTyper-v0.1.10-x86_64-linux-cpu-static-base-en/), with
# the binary, lib/ (cuda only), and any model files nested inside.
#
# CPU layout (truly static, no dynamic deps):
#   VoiceTyper-v...-cpu-static.../
#     VoiceTyper
#     stt_models/  (optional)
#     vad_models/  (optional)
#
# CUDA layout (self-contained bundle, see flake.nix cuda-static):
#   VoiceTyper-v...-cuda-static.../
#     bin/
#       VoiceTyper        # shell wrapper invoking lib/ld-linux + libexec/...
#       VoiceTyperBench
#     libexec/
#       VoiceTyper        # actual ELF binary (rpath $ORIGIN/../lib)
#       VoiceTyperBench
#     lib/                # full transitive .so closure + ld-linux + CUDA runtime
#     stt_models/  (optional)
#     vad_models/  (optional)
#
#   result_link  : nix result symlink (e.g. "result-static")
#   variant      : "cpu" | "cuda"
#   model_variant: "" | "base-en" | "base-en-silero"
package_static() {
	local result_link="$1"
	local variant="$2"
	local model_variant="$3"
	local suffix=""
	[ -n "$model_variant" ] && suffix="-${model_variant}"
	local name="VoiceTyper-v${VERSION}-${PLATFORM}-${variant}-static${suffix}.tar.gz"
	local top="${name%.tar.gz}"
	local stage="build/stage_${PLATFORM}_${variant}_static${suffix}"
	local root="$stage/$top"

	if [ -d "$stage" ]; then
		chmod -R u+w "$stage"
	fi
	rm -rf "$stage"
	mkdir -p "$root"

	if [ "$variant" = "cuda" ]; then
		cp -r "$result_link/bin" "$root/bin"
		cp -r "$result_link/libexec" "$root/libexec"
		cp -r "$result_link/lib" "$root/lib"
		chmod -R u+w "$root"
	else
		cp "$result_link/bin/VoiceTyper" "$root/VoiceTyper"
		chmod +w "$root/VoiceTyper"
	fi

	stage_models "$root" "$model_variant"

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

# Inject model files into an AppImage by extracting the squashfs, adding
# models into usr/share/voicetyper/, rewriting AppRun, and repackaging.
#   input         : source AppImage path
#   model_variant : "base-en" | "base-en-silero"
#   output_name   : filename for the new AppImage in dist/
bundle_appimage_models() {
	local input="$1"
	local model_variant="$2"
	local output_name="$3"
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
	local unsquashfs_bin mksquashfs_bin
	unsquashfs_bin=$(get_tool unsquashfs squashfsTools)
	mksquashfs_bin=$(get_tool mksquashfs squashfsTools)
	"$unsquashfs_bin" -no-progress -d "$work/squashfs-root" "$work/payload.squashfs" > /dev/null

	# Inject model files into the AppDir.
	local sq="$work/squashfs-root"
	stage_models "$sq/usr/share/voicetyper" "$model_variant"

	# Rewrite AppRun to point VOICETYPER_DATA_DIR at the bundled models.
	printf '%s\n' \
		'#!/bin/bash' \
		'dir="$(dirname "$(readlink -f "$0")")"' \
		'export VOICETYPER_DATA_DIR="$dir/usr/share/voicetyper"' \
		'exec "$dir/usr/bin/VoiceTyper" "$@"' \
		> "$sq/AppRun"
	chmod +x "$sq/AppRun"

	# Rebuild squashfs and reassemble the AppImage.
	"$mksquashfs_bin" "$sq" "$work/new-payload.squashfs" -comp xz -noappend -root-owned -no-progress > /dev/null
	cat "$work/runtime" "$work/new-payload.squashfs" > "$output"
	chmod +x "$output"

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
echo "    packaged VoiceTyper-v${VERSION}-${PLATFORM}-cpu-appimage.AppImage"
bundle_appimage_models "$CPU_APPIMAGE" "base-en" "VoiceTyper-v${VERSION}-${PLATFORM}-cpu-appimage-base-en.AppImage"
bundle_appimage_models "$CPU_APPIMAGE" "base-en-silero" "VoiceTyper-v${VERSION}-${PLATFORM}-cpu-appimage-base-en-silero.AppImage"

echo "--- static (cpu) ---"
START=$SECONDS
build_target static "result-static"
echo "    build took $((SECONDS - START))s"
package_static "result-static" "cpu" ""
package_static "result-static" "cpu" "base-en"
package_static "result-static" "cpu" "base-en-silero"

echo "--- appimage (cuda) ---"
START=$SECONDS
build_target cuda-appimage "result-cuda-appimage"
echo "    build took $((SECONDS - START))s"
CUDA_APPIMAGE=$(find_appimage "result-cuda-appimage" "VoiceTyper-*-x86_64-cuda.AppImage")
cp "$CUDA_APPIMAGE" "$DIST_DIR/VoiceTyper-v${VERSION}-${PLATFORM}-cuda-appimage.AppImage"
echo "    packaged VoiceTyper-v${VERSION}-${PLATFORM}-cuda-appimage.AppImage"
bundle_appimage_models "$CUDA_APPIMAGE" "base-en" "VoiceTyper-v${VERSION}-${PLATFORM}-cuda-appimage-base-en.AppImage"
bundle_appimage_models "$CUDA_APPIMAGE" "base-en-silero" "VoiceTyper-v${VERSION}-${PLATFORM}-cuda-appimage-base-en-silero.AppImage"

echo "--- static (cuda) ---"
START=$SECONDS
build_target cuda-static "result-cuda-static"
echo "    build took $((SECONDS - START))s"
package_static "result-cuda-static" "cuda" ""
package_static "result-cuda-static" "cuda" "base-en"
package_static "result-cuda-static" "cuda" "base-en-silero"

echo ""
echo "=== Done in $((SECONDS - TOTAL_START))s ==="
echo "Created:"
ls -1 "$DIST_DIR"
