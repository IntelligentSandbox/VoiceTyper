#!/usr/bin/env bash
#
# tools/release.sh - the single build / package / release entry point.
#
# Builds and packages the Windows (cpu + cuda) distributables into dist/. The
# v<VERSION> git tag and / or a DRAFT GitHub release are opt-in. Portable Linux
# outputs are opt-in (--linux), built on a remote NixOS box over ssh.
#
# Canonical invocations (operations compose via flags):
#   tools/release.sh                  build + package Windows                 -> dist/
#   tools/release.sh --release        build + package + DRAFT GitHub release
#   tools/release.sh --tag --release  cut tag + build + package + DRAFT release
#
# Add --linux to any of the above to also build + package the portable Linux
# outputs (static + AppImage, cpu + cuda) on the remote NixOS box and attach
# them to the same dist/ (and release).
#
# Only DRAFT releases are ever created automatically - publishing is a manual
# click in the GitHub UI.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

VERSION="$(tr -d '[:space:]' < VERSION)"
TAG="v$VERSION"
DIST_DIR="dist"
REMOTE="${VOICETYPER_RELEASE_REMOTE:-origin}"
NIX_SSH="${VOICETYPER_NIX_SSH:-rock}"
NIX_REPO="${VOICETYPER_NIX_REPO:-~/repos/VoiceTyper}"
CHANGELOG_FILE="$DIST_DIR/release-notes-$TAG.md"
VAD_MODEL="vad_models/ggml-silero-v5.1.2.bin"
LINUX_PLATFORM="x86_64-linux"

DO_TAG=0
DO_RELEASE=0
DO_LINUX=0
USE_CCACHE=0
NOTES_FILE=""
INTERNAL_WINDOWS_BUILD=0
INTERNAL_LINUX_PACKAGE=0

usage() {
	cat <<EOF
Usage: tools/release.sh [options]

Builds and packages the Windows cpu/cuda outputs into dist/. Optionally cuts
the v<VERSION> git tag and / or creates a DRAFT GitHub release.

Canonical invocations (flags compose):
  tools/release.sh                  build + package Windows
  tools/release.sh --release        build + package + DRAFT GitHub release
  tools/release.sh --tag --release  cut tag + build + package + DRAFT release

Operations:
  (default)         build + package Windows only.
  --release         also create a DRAFT GitHub release from dist/ artifacts.
                    Requires the v<VERSION> tag to exist on the remote (pass
                    --tag to cut it as part of the same run).
  --tag             also cut + push the v<VERSION> git tag. Created before the
                    build so the binary embeds the clean version string; pushed
                    only after the build + package succeeds.

Options:
  --linux             Also build + package portable Linux (static + AppImage,
                      cpu + cuda) on the remote NixOS box over ssh.
  --ccache            Enable ccache for the CUDA nix builds (Linux). Auto-enabled
                      when \$CCACHE_DIR (default /var/cache/voicetyper-ccache)
                      exists on the NixOS box.
  --remote NAME       Git remote to push the tag to (env: VOICETYPER_RELEASE_REMOTE, default: origin).
  --nix-ssh TARGET    ssh target for the NixOS box (env: VOICETYPER_NIX_SSH, default: rock).
  --nix-repo PATH     Repo path on the NixOS box (env: VOICETYPER_NIX_REPO, default: ~/repos/VoiceTyper).
  --notes-file PATH   Use PATH as the release notes instead of the auto-generated git changelog.
  -h|--help           Show this help.

Draft-only: this script never publishes a release. Promote a draft from the
GitHub UI when ready.
EOF
}

die() {
	echo "Error: $*" >&2
	exit 1
}

require_command() {
	command -v "$1" >/dev/null 2>&1 || die "Required command '$1' was not found."
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--tag) DO_TAG=1 ;;
		--release) DO_RELEASE=1 ;;
		--linux) DO_LINUX=1 ;;
		--ccache) USE_CCACHE=1 ;;
		--remote) [ "$#" -ge 2 ] || die "--remote requires a name."; REMOTE="$2"; shift ;;
		--nix-ssh) [ "$#" -ge 2 ] || die "--nix-ssh requires a target."; NIX_SSH="$2"; shift ;;
		--nix-repo) [ "$#" -ge 2 ] || die "--nix-repo requires a path."; NIX_REPO="$2"; shift ;;
		--notes-file) [ "$#" -ge 2 ] || die "--notes-file requires a path."; NOTES_FILE="$2"; shift ;;
		--internal-windows-build) INTERNAL_WINDOWS_BUILD=1 ;;
		--internal-linux-package) INTERNAL_LINUX_PACKAGE=1 ;;
		-h|--help) usage; exit 0 ;;
		*) die "Unknown option '$1'. Try --help." ;;
	esac
	shift
done

[ -n "$VERSION" ] || die "VERSION is empty."

# ---------------------------------------------------------------------------
# Windows build + package
# ---------------------------------------------------------------------------

sync_asset_dir() {
	local source_dir="$1"
	local output_dir="$2"

	mkdir -p "$output_dir"
	if command -v rsync >/dev/null 2>&1; then
		rsync -a --delete "$source_dir/" "$output_dir/"
		return
	fi
	cp -ru "$source_dir/." "$output_dir/"
}

windows_build() {
	local build_type="Release"
	local build_jobs="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"
	local cuda_path="${VOICETYPER_CUDA_PATH:-C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2}"

	local cpu_build_dir="build/cpu"
	local cpu_output_dir="build/${build_type}_cpu"

	echo "=== Building Release (CPU) ==="
	local start=$SECONDS
	cmake -S . -B "$cpu_build_dir" -G "Visual Studio 17 2022" -A x64 \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=OFF
	cmake --build "$cpu_build_dir" --config "$build_type" --parallel "$build_jobs"
	sync_asset_dir stt_models "$cpu_output_dir/stt_models"
	sync_asset_dir vad_models "$cpu_output_dir/vad_models"
	cp -u media/voicetyper-icon.ico "$cpu_output_dir/app.ico"
	touch "$cpu_output_dir/settings.ini"
	rm -rf "$cpu_output_dir/media" "$cpu_output_dir/data"
	rm -f "$cpu_output_dir/VoiceTyperBench.exe" "$cpu_output_dir/voicetyper-icon.ico" "$cpu_output_dir/voicetyper-icon.png"
	echo "    CPU build took $((SECONDS - start))s"

	echo "=== Building Release (CUDA plugin) ==="
	start=$SECONDS
	local plugin_build_dir="build/cuda-plugin"
	local plugin_output_dir="build/${build_type}_cuda-plugin"
	local cuda_output_dir="build/${build_type}_cuda"

	cmake -S . -B "$plugin_build_dir" -G "Visual Studio 17 2022" -A x64 \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=ON \
		-DCUDAToolkit_ROOT="$cuda_path"
	cmake --build "$plugin_build_dir" --config "$build_type" --target ggml-cuda --parallel "$build_jobs"

	rm -rf "$cuda_output_dir"
	cp -a "$cpu_output_dir" "$cuda_output_dir"
	mkdir -p "$cuda_output_dir/cuda"

	local ggml_cuda_dll=""
	local candidate
	for candidate in \
		"$plugin_output_dir/ggml-cuda.dll" \
		"$plugin_build_dir/bin/$build_type/ggml-cuda.dll" \
		"$plugin_build_dir/bin/ggml-cuda.dll"; do
		if [ -f "$candidate" ]; then
			ggml_cuda_dll="$candidate"
			break
		fi
	done
	[ -n "$ggml_cuda_dll" ] || die "ggml-cuda.dll not found in cuda-plugin build output."
	cp -u "$ggml_cuda_dll" "$cuda_output_dir/cuda/"

	local cuda_dll_dir="$cuda_path/bin"
	if [ -d "$cuda_path/bin/x64" ]; then cuda_dll_dir="$cuda_path/bin/x64"; fi
	cp -u "$cuda_dll_dir"/cublas64_*.dll "$cuda_output_dir/cuda/"
	cp -u "$cuda_dll_dir"/cublasLt64_*.dll "$cuda_output_dir/cuda/"
	cp -u "$cuda_dll_dir"/cudart64_*.dll "$cuda_output_dir/cuda/"
	echo "    CUDA plugin build took $((SECONDS - start))s"
}

copy_build_output() {
	local source_dir="$1"
	local target_dir="$2"

	rm -rf "$target_dir"
	mkdir -p "$(dirname "$target_dir")"
	cp -a "$source_dir" "$target_dir"
}

remove_model_files() {
	rm -rf "$1/stt_models" "$1/vad_models"
}

zip_dir() {
	local source_dir="$1"
	local output_zip="$2"
	local absolute_output_zip

	absolute_output_zip="$(pwd)/$output_zip"
	(cd "$source_dir" && 7z a -tzip "$absolute_output_zip" -r . > /dev/null)
}

build_msi() {
	local build_output="$1"
	local output_path="$2"

	wix build -o "$output_path" -pdbtype none \
		-d "BuildOutput=$(pwd)/$build_output" \
		-d "ProductVersion=$VERSION" \
		packaging/VoiceTyper.wxs
}

zip_cuda_addon() {
	local source_build="$1"
	local output_zip="$2"
	local absolute_output_zip

	absolute_output_zip="$(pwd)/$output_zip"
	(cd "$source_build" && 7z a -tzip "$absolute_output_zip" cuda/ > /dev/null)
}

JOB_PIDS=()
JOB_NAMES=()

run_job() {
	local job_name="$1"
	shift

	echo "--- $job_name"
	"$@" &
	JOB_PIDS+=("$!")
	JOB_NAMES+=("$job_name")
}

wait_for_jobs() {
	local failed=0
	local i

	for i in "${!JOB_PIDS[@]}"; do
		if wait "${JOB_PIDS[$i]}"; then
			echo "=== ${JOB_NAMES[$i]} done ==="
		else
			echo "Error: ${JOB_NAMES[$i]} failed." >&2
			failed=1
		fi
	done

	[ "$failed" = "0" ] || exit 1
}

windows_package() {
	local platform="x64_win"
	local cpu_build="build/Release_cpu"
	local cuda_build="build/Release_cuda"
	local stage_dir="build/package_${platform}"
	local cpu_stage="$stage_dir/cpu"
	local cuda_stage="$stage_dir/cuda"
	local cpu_zip="$DIST_DIR/VoiceTyper-v${VERSION}-${platform}-cpu.zip"
	local cuda_zip="$DIST_DIR/VoiceTyper-v${VERSION}-${platform}-cuda.zip"
	local cuda_addon_zip="$DIST_DIR/VoiceTyper-v${VERSION}-${platform}-cuda-addon.zip"
	local cpu_msi="$DIST_DIR/VoiceTyper-v${VERSION}-${platform}-cpu.msi"
	local cuda_msi="$DIST_DIR/VoiceTyper-v${VERSION}-${platform}-cuda.msi"

	[ -f "$cpu_build/VoiceTyper.exe" ] || die "CPU build output '$cpu_build' missing; build failed?"
	[ -f "$cuda_build/VoiceTyper.exe" ] || die "CUDA build output '$cuda_build' missing; build failed?"

	rm -rf "$DIST_DIR" "$stage_dir"
	mkdir -p "$DIST_DIR"

	echo "=== Staging package inputs ($platform) ==="
	local start=$SECONDS
	copy_build_output "$cuda_build" "$cuda_stage"
	remove_model_files "$cuda_stage"
	copy_build_output "$cpu_build" "$cpu_stage"
	remove_model_files "$cpu_stage"
	echo "    Staging took $((SECONDS - start))s"

	echo "=== Creating package artifacts ($platform) ==="
	start=$SECONDS
	JOB_PIDS=()
	JOB_NAMES=()
	run_job "CUDA zip" zip_dir "$cuda_stage" "$cuda_zip"
	run_job "CUDA MSI" build_msi "$cuda_stage" "$cuda_msi"
	run_job "CUDA add-on zip" zip_cuda_addon "$cuda_build" "$cuda_addon_zip"
	run_job "CPU zip" zip_dir "$cpu_stage" "$cpu_zip"
	run_job "CPU MSI" build_msi "$cpu_stage" "$cpu_msi"
	wait_for_jobs
	echo "    Package artifacts took $((SECONDS - start))s"
}

# ---------------------------------------------------------------------------
# Portable Linux packaging (runs on the remote NixOS box)
# ---------------------------------------------------------------------------

build_nix() {
	local package="$1"
	local out_link="$2"
	local use_ccache="$3"
	local ccache_args=()

	if [ "$use_ccache" = "1" ]; then
		local dir="${CCACHE_DIR:-/var/cache/voicetyper-ccache}"
		[ -d "$dir" ] || die "ccache dir $dir does not exist."
		export VOICETYPER_CCACHE=1
		export CCACHE_DIR="$dir"
		ccache_args=(--impure --option extra-sandbox-paths "$dir")
		echo "[linux] ccache on -> $dir"
	fi
	echo "[linux] building .#$package"
	nix build ".#$package" --out-link "$out_link" "${ccache_args[@]}"
}

find_appimage() {
	local out_link="$1"
	local pattern="$2"
	local src

	src="$(find -L "$out_link" -name "$pattern" -type f | head -n 1)"
	[ -n "$src" ] || die "no artifact matching '$pattern' under $out_link."
	echo "$src"
}

package_static() {
	local result_link="$1"
	local variant="$2"
	local name="VoiceTyper-v${VERSION}-${LINUX_PLATFORM}-${variant}-static.tar.gz"
	local top="${name%.tar.gz}"
	local stage="build/stage_${LINUX_PLATFORM}_${variant}_static"
	local root="$stage/$top"

	if [ -d "$stage" ]; then chmod -R u+w "$stage"; fi
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

bundle_appimage_vad() {
	local input="$1"
	local output_name="$2"
	local output="$DIST_DIR/$output_name"
	local work="build/appimage_repack"

	local e_shoff e_shentsize e_shnum elf_size
	e_shoff=$(od -A n -t u8 --endian=little -j 40 -N 8 "$input" | tr -d ' ')
	e_shentsize=$(od -A n -t u2 --endian=little -j 58 -N 2 "$input" | tr -d ' ')
	e_shnum=$(od -A n -t u2 --endian=little -j 60 -N 2 "$input" | tr -d ' ')
	elf_size=$((e_shoff + e_shentsize * e_shnum))

	rm -rf "$work"
	mkdir -p "$work"

	head -c "$elf_size" "$input" > "$work/runtime"
	chmod +x "$work/runtime"
	tail -c "+$((elf_size + 1))" "$input" > "$work/payload.squashfs"

	if command -v unsquashfs >/dev/null 2>&1; then
		unsquashfs -no-progress -d "$work/squashfs-root" "$work/payload.squashfs" > /dev/null
	else
		local store_path
		store_path=$(nix build "nixpkgs#squashfsTools" --no-link --print-out-paths 2>/dev/null) || \
			store_path=$(nix-build '<nixpkgs>' -A squashfsTools --no-out-link 2>/dev/null)
		[ -n "$store_path" ] || die "could not find unsquashfs via PATH or nix."
		"$store_path/bin/unsquashfs" -no-progress -d "$work/squashfs-root" "$work/payload.squashfs" > /dev/null
	fi

	local sq="$work/squashfs-root"
	mkdir -p "$sq/usr/share/voicetyper/vad_models"
	cp "$VAD_MODEL" "$sq/usr/share/voicetyper/vad_models/"

	printf '%s\n' \
		'#!/bin/bash' \
		'dir="$(dirname "$(readlink -f "$0")")"' \
		'export VOICETYPER_DATA_DIR="$dir/usr/share/voicetyper"' \
		'exec "$dir/usr/bin/VoiceTyper" "$@"' \
		> "$sq/AppRun"
	chmod +x "$sq/AppRun"

	if command -v mksquashfs >/dev/null 2>&1; then
		mksquashfs "$sq" "$work/new-payload.squashfs" -comp xz -noappend -root-owned -no-progress > /dev/null
	else
		local store_path
		store_path=$(nix build "nixpkgs#squashfsTools" --no-link --print-out-paths 2>/dev/null) || \
			store_path=$(nix-build '<nixpkgs>' -A squashfsTools --no-out-link 2>/dev/null)
		[ -n "$store_path" ] || die "could not find mksquashfs via PATH or nix."
		"$store_path/bin/mksquashfs" "$sq" "$work/new-payload.squashfs" -comp xz -noappend -root-owned -no-progress > /dev/null
	fi
	cat "$work/runtime" "$work/new-payload.squashfs" > "$output"
	chmod +w "$output"

	rm -rf "$work"
	echo "    packaged $output_name"
}

linux_package() {
	[ -f "$VAD_MODEL" ] || die "VAD model file '$VAD_MODEL' not found."

	local want_ccache=0
	[ "$USE_CCACHE" = "1" ] && want_ccache=1
	[ -d "${CCACHE_DIR:-/var/cache/voicetyper-ccache}" ] && want_ccache=1

	rm -rf "$DIST_DIR"
	mkdir -p "$DIST_DIR"

	echo "=== Building portable Linux packages ($LINUX_PLATFORM) for v$VERSION ==="

	echo "--- appimage (cpu) ---"
	local start=$SECONDS
	build_nix appimage result-appimage 0
	echo "    build took $((SECONDS - start))s"
	local cpu_appimage
	cpu_appimage=$(find_appimage result-appimage "VoiceTyper-*-x86_64.AppImage")
	cp "$cpu_appimage" "$DIST_DIR/VoiceTyper-v${VERSION}-${LINUX_PLATFORM}-cpu-appimage.AppImage"
	bundle_appimage_vad "$cpu_appimage" "VoiceTyper-v${VERSION}-${LINUX_PLATFORM}-cpu-appimage.AppImage"

	echo "--- static (cpu) ---"
	start=$SECONDS
	build_nix static result-static 0
	echo "    build took $((SECONDS - start))s"
	package_static result-static cpu

	echo "--- appimage (cuda) ---"
	start=$SECONDS
	build_nix cuda-appimage result-cuda-appimage "$want_ccache"
	echo "    build took $((SECONDS - start))s"
	local cuda_appimage
	cuda_appimage=$(find_appimage result-cuda-appimage "VoiceTyper-*-x86_64-cuda.AppImage")
	cp "$cuda_appimage" "$DIST_DIR/VoiceTyper-v${VERSION}-${LINUX_PLATFORM}-cuda-appimage.AppImage"
	bundle_appimage_vad "$cuda_appimage" "VoiceTyper-v${VERSION}-${LINUX_PLATFORM}-cuda-appimage.AppImage"

	echo "--- static (cuda) ---"
	start=$SECONDS
	build_nix cuda-static result-cuda-static "$want_ccache"
	echo "    build took $((SECONDS - start))s"
	package_static result-cuda-static cuda

	echo "=== Linux packaging done ==="
	echo "Created:"
	ls -1 "$DIST_DIR"
}

# ---------------------------------------------------------------------------
# Tag + release helpers
# ---------------------------------------------------------------------------

tag_exists_locally() {
	git rev-parse -q --verify "refs/tags/$TAG" >/dev/null 2>&1
}

tag_exists_remote() {
	git ls-remote --exit-code --tags "$REMOTE" "refs/tags/$TAG" >/dev/null 2>&1
}

release_exists() {
	gh release view "$TAG" >/dev/null 2>&1
}

require_clean_tree() {
	if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
		git status --short
		die "Commit or stash tracked changes first."
	fi
}

previous_tag() {
	git tag --sort=-v:refname | grep -vxF "$TAG" | head -n 1 || true
}

generate_notes() {
	local previous
	local range

	previous="$(previous_tag)"
	if [ -n "$previous" ]; then range="$previous..HEAD"; else range="HEAD"; fi

	mkdir -p "$(dirname "$CHANGELOG_FILE")"
	{
		printf "# VoiceTyper %s\n\n" "$TAG"
		if [ -n "$previous" ]; then printf "Changes since %s:\n\n" "$previous"; else printf "Changes:\n\n"; fi
		git log "$range" --pretty=format:'- %s (%h)'
		printf "\n"
	} > "$CHANGELOG_FILE"
}

collect_assets() {
	local file basename

	RELEASE_ASSETS=()
	shopt -s nullglob
	for file in "$DIST_DIR"/*; do
		if [ ! -f "$file" ]; then continue; fi
		case "$file" in *.md) continue ;; esac
		basename="$(basename "$file")"
		case "$basename" in *-v${VERSION}-*) ;; *) die "Artifact '$basename' does not match version $VERSION." ;; esac
		RELEASE_ASSETS+=("$file")
	done
	shopt -u nullglob

	[ "${#RELEASE_ASSETS[@]}" -gt 0 ] || die "No package artifacts found in $DIST_DIR."
}

# ---------------------------------------------------------------------------
# Internal dispatch entry points. The Windows .bat wrappers re-invoke this
# script to run a single modular function below. These must sit after every
# function definition above so they are all in scope.
# ---------------------------------------------------------------------------

if [ "$INTERNAL_WINDOWS_BUILD" = "1" ]; then
	require_command cmake
	windows_build
	exit 0
fi

# Portable Linux packaging runs ON the remote NixOS box (the Windows host
# re-invokes this script there with --internal-linux-package).
if [ "$INTERNAL_LINUX_PACKAGE" = "1" ]; then
	require_command nix
	linux_package
	exit 0
fi

# ---------------------------------------------------------------------------
# Command requirements
# ---------------------------------------------------------------------------

require_command cmake
require_command 7z
require_command wix

if [ "$DO_TAG" = "1" ] || [ "$DO_RELEASE" = "1" ]; then
	require_command git
	require_command gh
fi

if [ "$DO_LINUX" = "1" ]; then
	require_command ssh
	require_command scp
	[ -f "$VAD_MODEL" ] || die "Model file '$VAD_MODEL' not found (needed by the Linux portable bundles)."
fi

# ---------------------------------------------------------------------------
# Pre-flight: tag / release state
# ---------------------------------------------------------------------------

if [ "$DO_TAG" = "1" ] || [ "$DO_RELEASE" = "1" ]; then
	require_clean_tree
	if [ "$DO_TAG" = "1" ]; then
		tag_exists_locally && die "Local tag $TAG already exists."
		tag_exists_remote && die "Remote tag $TAG already exists on $REMOTE."
		release_exists && die "GitHub release $TAG already exists."
	else
		# --release without --tag: the tag must already exist on the remote so
		# gh release create --verify-tag succeeds.
		tag_exists_remote || die "Remote tag $TAG does not exist on $REMOTE. Pass --tag to cut it."
		release_exists && die "GitHub release $TAG already exists."
	fi
fi

# ---------------------------------------------------------------------------
# Step 1: optional local tag (before the build so the version string is clean)
# ---------------------------------------------------------------------------

if [ "$DO_TAG" = "1" ]; then
	echo "=== Cutting local tag $TAG at HEAD ==="
	git tag -a "$TAG" -m "VoiceTyper $TAG"
	cleanup_tag() {
		if tag_exists_locally && ! tag_exists_remote; then
			echo "Cleaning up unpushed local tag $TAG due to failure." >&2
			git tag -d "$TAG" >/dev/null
		fi
	}
	trap cleanup_tag EXIT
fi

# ---------------------------------------------------------------------------
# Step 2: build + package Windows
# ---------------------------------------------------------------------------

TOTAL_START=$SECONDS
windows_build
echo ""
windows_package

# ---------------------------------------------------------------------------
# Step 3: optional build + package Linux (remote)
# ---------------------------------------------------------------------------

if [ "$DO_LINUX" = "1" ]; then
	echo ""
	echo "=== Linux build + package (remote $NIX_SSH:$NIX_REPO) ==="
	start=$SECONDS
	commit="$(git rev-parse HEAD)"

	echo "Syncing remote tree to $commit..."
	ssh "$NIX_SSH" "cd $NIX_REPO && git fetch --all --tags && git checkout '$commit'"

	echo "Staging VAD model file to the NixOS box..."
	tar -cf - "$VAD_MODEL" | ssh "$NIX_SSH" "cd $NIX_REPO && tar -xf -"

	echo "Building + packaging on the NixOS box..."
	ccache_arg=""
	[ "$USE_CCACHE" = "1" ] && ccache_arg="--ccache"
	ssh "$NIX_SSH" "cd $NIX_REPO && bash tools/release.sh --internal-linux-package $ccache_arg"

	echo "Streaming Linux artifacts back into local $DIST_DIR/..."
	ssh "$NIX_SSH" "cd $NIX_REPO && tar -cf - -C $DIST_DIR ." | tar -xf - -C "$DIST_DIR"

	echo "    Linux step took $((SECONDS - start))s"
fi

# ---------------------------------------------------------------------------
# Step 4: optional draft GitHub release
# ---------------------------------------------------------------------------

if [ "$DO_RELEASE" = "1" ]; then
	echo ""
	echo "=== Preparing draft GitHub release $TAG ==="

	local_notes="$CHANGELOG_FILE"
	[ -n "$NOTES_FILE" ] && local_notes="$NOTES_FILE"

	if [ ! -f "$local_notes" ]; then
		echo "Generating release notes (git-based)..."
		generate_notes
		local_notes="$CHANGELOG_FILE"
	else
		echo "Using existing release notes at $local_notes"
	fi

	collect_assets
	echo "Found ${#RELEASE_ASSETS[@]} artifact(s) for $TAG:"
	for asset in "${RELEASE_ASSETS[@]}"; do
		echo "    $(basename "$asset")"
	done

	if [ "$DO_TAG" = "1" ]; then
		echo "Pushing tag to $REMOTE..."
		git push "$REMOTE" "$TAG"
		trap - EXIT
	fi

	echo "Creating draft GitHub release..."
	gh release create "$TAG" "${RELEASE_ASSETS[@]}" \
		--title "VoiceTyper $TAG" \
		--notes-file "$local_notes" \
		--verify-tag \
		--draft

	echo "=== Draft release $TAG created ==="
elif [ "$DO_TAG" = "1" ]; then
	echo "Pushing tag to $REMOTE..."
	git push "$REMOTE" "$TAG"
	trap - EXIT
	echo "=== Tag $TAG pushed ==="
fi

echo ""
echo "=== Done in $((SECONDS - TOTAL_START))s ==="
echo "Created:"
ls -1 "$DIST_DIR" 2>/dev/null || true
