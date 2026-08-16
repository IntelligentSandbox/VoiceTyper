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
# outputs (flat tar.gz bundles, cpu + cuda) on the remote NixOS box and attach
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
REMOTE="${VOICETYPER_RELEASE_REMOTE:-github}"
NIX_SSH="${VOICETYPER_NIX_SSH:-rock}"
NIX_REPO="${VOICETYPER_NIX_REPO:-~/repos/VoiceTyper}"
NIX_GIT_REMOTE="${VOICETYPER_NIX_GIT_REMOTE:-gitea}"
NIX_RELEASE_REPO="${VOICETYPER_NIX_RELEASE_REPO:-}"
CHANGELOG_FILE="$DIST_DIR/release-notes-$TAG.md"
LINUX_PLATFORM="x86_64-linux"

DO_TAG=0
DO_RELEASE=0
DO_LINUX=0
USE_CCACHE=0
NOTES_FILE=""
INTERNAL_WINDOWS_BUILD=0
INTERNAL_LINUX_PACKAGE=0
INTERNAL_LINUX_BUILD=0

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
                    only after the build + package succeeds. Any existing local
                    + remote tag for v<VERSION> is removed first, unless a
                    GitHub release for that tag already exists.

Options:
  --linux             Also build + package portable Linux (flat tar.gz bundles,
                      cpu + cuda) on the remote NixOS box over ssh.
  --ccache            Enable ccache for the nix builds (Linux, cpu + cuda).
                      Auto-enabled when \$CCACHE_DIR (default
                      /var/cache/voicetyper-ccache) exists on the NixOS box.
                      The cpu build warms the cache the cuda build then hits.
  --remote NAME       Git remote the v<VERSION> tag is pushed to for the GitHub
                      release (env: VOICETYPER_RELEASE_REMOTE, default: github).
  --nix-ssh TARGET    ssh target for the NixOS box (env: VOICETYPER_NIX_SSH, default: rock).
  --nix-git-remote NAME
                      Code-tracking remote the release commit is synced to the
                      NixOS box from for --linux builds; unpushed commits are
                      pushed there first (env: VOICETYPER_NIX_GIT_REMOTE,
                      default: gitea).
  --nix-repo PATH     Dev repo on the NixOS box, only used as a local-clone seed to
                      bootstrap the release tree; never checked out or otherwise
                      modified (env: VOICETYPER_NIX_REPO, default: ~/repos/VoiceTyper).
  --nix-release-repo PATH
                      Dedicated release source tree on the NixOS box for --linux
                      builds; force-checked-out + cleaned to the release commit
                      every run (env: VOICETYPER_NIX_RELEASE_REPO, default:
                      <nix-repo>-release). ccache is global and still shared.
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
		--nix-git-remote) [ "$#" -ge 2 ] || die "--nix-git-remote requires a name."; NIX_GIT_REMOTE="$2"; shift ;;
		--nix-repo) [ "$#" -ge 2 ] || die "--nix-repo requires a path."; NIX_REPO="$2"; shift ;;
		--nix-release-repo) [ "$#" -ge 2 ] || die "--nix-release-repo requires a path."; NIX_RELEASE_REPO="$2"; shift ;;
		--notes-file) [ "$#" -ge 2 ] || die "--notes-file requires a path."; NOTES_FILE="$2"; shift ;;
		--internal-windows-build) INTERNAL_WINDOWS_BUILD=1 ;;
		--internal-linux-package) INTERNAL_LINUX_PACKAGE=1 ;;
		--internal-linux-build) INTERNAL_LINUX_BUILD=1 ;;
		-h|--help) usage; exit 0 ;;
		*) die "Unknown option '$1'. Try --help." ;;
	esac
	shift
done

[ -n "$VERSION" ] || die "VERSION is empty."

# Resolved after arg parsing so --nix-repo also moves the default release tree.
NIX_RELEASE_REPO="${NIX_RELEASE_REPO:-${NIX_REPO}-release}"

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

# Strip files that only exist because the app was run from this build output
# dir during development, so they never ship inside a distributable.
# voicetyper-crash-* is runtime output; settings.ini is reset to empty so
# no dev contents leak (the app rewrites it on the user's first settings change,
# and the MSI ships it as a Permanent/NeverOverwrite component).
strip_runtime_artifacts() {
	local dir="$1"
	rm -f "$dir"/voicetyper-crash-*
	: > "$dir/settings.ini"
}

# Pick the cmake generator for the Windows build. Prefer Ninja (faster configure
# and better parallel scheduling, especially for incremental rebuilds) when the
# MSVC toolchain is on PATH (i.e. run from a VS "x64 Native Tools" shell or with
# vcvars loaded). Fall back to the Visual Studio generator, which locates MSVC
# via the registry and does not need cl.exe on PATH. Override either way with
# $VOICETYPER_CMAKE_GENERATOR.
windows_cmake_generator() {
	if [ -n "${VOICETYPER_CMAKE_GENERATOR:-}" ]; then
		echo "$VOICETYPER_CMAKE_GENERATOR"
		return
	fi
	if command -v cl >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
		echo "Ninja"
	else
		echo "Visual Studio 17 2022"
	fi
}

# Generator-specific configure flags. The VS generator is multi-config and takes
# -A x64; Ninja is single-config and takes -DCMAKE_BUILD_TYPE=Release instead.
# (Ninja rejects -A, and -A is pointless for it.)
windows_cmake_configure_flags() {
	local generator="$1"
	case "$generator" in
		Ninja) echo "-DCMAKE_BUILD_TYPE=Release" ;;
		*) echo "-A x64" ;;
	esac
}

# CPU build: configure + build the CPU exe and stage runtime assets into the
# Release_cpu output dir. Runs as a background job in parallel with the CUDA
# plugin build.
windows_build_cpu() {
	local build_type="Release"
	local build_jobs="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"
	local generator="$1"
	local cpu_build_dir="build/cpu"
	local cpu_output_dir="build/${build_type}_cpu"

	echo "=== Building Release (CPU) ==="
	local start=$SECONDS
	cmake -S . -B "$cpu_build_dir" -G "$generator" \
		$(windows_cmake_configure_flags "$generator") \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=OFF
	cmake --build "$cpu_build_dir" --config "$build_type" --parallel "$build_jobs"
	sync_asset_dir stt_models "$cpu_output_dir/stt_models"
	sync_asset_dir vad_models "$cpu_output_dir/vad_models"
	cp -u media/voicetyper-icon.ico "$cpu_output_dir/app.ico"
	rm -rf "$cpu_output_dir/media" "$cpu_output_dir/data"
	rm -f "$cpu_output_dir/VoiceTyperBench.exe" "$cpu_output_dir/voicetyper-icon.ico" "$cpu_output_dir/voicetyper-icon.png"
	strip_runtime_artifacts "$cpu_output_dir"
	echo "    CPU build took $((SECONDS - start))s"
}

# CUDA plugin build: configure + build ONLY the ggml-cuda target. Does not touch
# the Release_cpu / Release_cuda output dirs. Runs as a background job in
# parallel with the CPU build - the nvcc kernel compile is the long pole, so
# overlapping it with the CPU build's MSVC compiles keeps all cores busy without
# oversubscribing (nvcc leaves headroom that MSVC /MP fills).
windows_build_cuda_plugin() {
	local build_type="Release"
	local build_jobs="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"
	local cuda_path="${VOICETYPER_CUDA_PATH:-C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2}"
	local generator="$1"
	local plugin_build_dir="build/cuda-plugin"

	echo "=== Building Release (CUDA plugin) ==="
	local start=$SECONDS
	cmake -S . -B "$plugin_build_dir" -G "$generator" \
		$(windows_cmake_configure_flags "$generator") \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=ON \
		-DCUDAToolkit_ROOT="$cuda_path"
	cmake --build "$plugin_build_dir" --config "$build_type" --target ggml-cuda --parallel "$build_jobs"
	echo "    CUDA plugin build took $((SECONDS - start))s"
}

# Assemble the CUDA output dir from the CPU output + the freshly built plugin.
# Runs AFTER both parallel builds finish (it needs the CPU output dir and the
# plugin .dll to both exist).
windows_assemble_cuda() {
	local build_type="Release"
	local cuda_path="${VOICETYPER_CUDA_PATH:-C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2}"
	local plugin_build_dir="build/cuda-plugin"
	local plugin_output_dir="build/${build_type}_cuda-plugin"
	local cuda_output_dir="build/${build_type}_cuda"
	local cpu_output_dir="build/${build_type}_cpu"

	rm -rf "$cuda_output_dir"
	cp -a "$cpu_output_dir" "$cuda_output_dir"

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
	cp -u "$ggml_cuda_dll" "$cuda_output_dir/"

	local cuda_dll_dir="$cuda_path/bin"
	if [ -d "$cuda_path/bin/x64" ]; then cuda_dll_dir="$cuda_path/bin/x64"; fi
	cp -u "$cuda_dll_dir"/cublas64_*.dll "$cuda_output_dir/"
	cp -u "$cuda_dll_dir"/cublasLt64_*.dll "$cuda_output_dir/"
	cp -u "$cuda_dll_dir"/cudart64_*.dll "$cuda_output_dir/"
}

windows_build() {
	local generator
	generator="$(windows_cmake_generator)"
	echo "=== Windows build (generator: $generator) ==="

	local start=$SECONDS
	JOB_PIDS=()
	JOB_NAMES=()
	run_job "CPU build" windows_build_cpu "$generator"
	run_job "CUDA plugin build" windows_build_cuda_plugin "$generator"
	wait_for_jobs
	windows_assemble_cuda
	echo "    Windows builds took $((SECONDS - start))s"
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
	mkdir -p "$1/stt_models" "$1/vad_models"
}

zip_dir() {
	local source_dir="$1"
	local output_zip="$2"
	local absolute_output_zip

	absolute_output_zip="$(pwd)/$output_zip"
	# settings.ini ships empty as the MSI's Permanent/NeverOverwrite component;
	# the portable zip excludes it so a fresh extract has no local config (the
	# app writes it on the user's first settings change).
	(cd "$source_dir" && 7z a -tzip "$absolute_output_zip" -r . -x!settings.ini > /dev/null)
}

build_msi() {
	local build_output="$1"
	local output_path="$2"

	wix build -o "$output_path" -pdbtype none \
		-d "BuildOutput=$(pwd)/$build_output" \
		-d "ProductVersion=$VERSION" \
		packaging/VoiceTyper.wxs
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
	run_job "CPU zip" zip_dir "$cpu_stage" "$cpu_zip"
	run_job "CPU MSI" build_msi "$cpu_stage" "$cpu_msi"
	wait_for_jobs
	echo "    Package artifacts took $((SECONDS - start))s"
}

# ---------------------------------------------------------------------------
# Local Linux dev build. Run inside the nix devShell (tools/build.sh re-invokes
# this script with --internal-linux-build). Mirrors windows_build() but uses a
# plain cmake generate + build (Ninja when available, else Unix Makefiles) so
# the dev loop avoids the full nix derivation. CPU is always built; the CUDA
# plugin is added on top when a CUDA toolkit is detected (the default nix
# devShell ships none, so it is skipped there with a notice).
# ---------------------------------------------------------------------------

linux_build() {
	local build_type="Release"
	local build_jobs="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"
	local generator
	if command -v ninja >/dev/null 2>&1; then
		generator="Ninja"
	else
		generator="Unix Makefiles"
	fi

	local cpu_build_dir="build/cpu"
	local cpu_output_dir="build/${build_type}_cpu"

	echo "=== Building Release (CPU) ==="
	local start=$SECONDS
	cmake -S . -B "$cpu_build_dir" -G "$generator" \
		-DCMAKE_BUILD_TYPE="$build_type" \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=OFF \
		-DVOICETYPER_APP_IPO=OFF
	cmake --build "$cpu_build_dir" --parallel "$build_jobs"
	[ -d stt_models ] && sync_asset_dir stt_models "$cpu_output_dir/stt_models"
	[ -d vad_models ] && sync_asset_dir vad_models "$cpu_output_dir/vad_models"
	strip_runtime_artifacts "$cpu_output_dir"
	echo "    CPU build took $((SECONDS - start))s"

	# The CUDA plugin is only built when a CUDA toolkit is available. The
	# default nix devShell ships no CUDA, so this is skipped there; on a CUDA
	# box the full CPU + CUDA pair is produced, mirroring tools/build.bat.
	local cuda_toolkit=""
	if command -v nvcc >/dev/null 2>&1; then
		cuda_toolkit="$(cd "$(dirname "$(command -v nvcc)")/.." && pwd)"
	elif [ -n "${CUDA_PATH:-}" ] && [ -x "${CUDA_PATH}/bin/nvcc" ]; then
		cuda_toolkit="$CUDA_PATH"
	fi
	if [ -z "$cuda_toolkit" ]; then
		echo "=== Skipping CUDA plugin build (no CUDA toolkit found) ==="
		return 0
	fi

	echo "=== Building Release (CUDA plugin) ==="
	start=$SECONDS
	local plugin_build_dir="build/cuda-plugin"
	local plugin_output_dir="build/${build_type}_cuda-plugin"
	local cuda_output_dir="build/${build_type}_cuda"

	cmake -S . -B "$plugin_build_dir" -G "$generator" \
		-DCMAKE_BUILD_TYPE="$build_type" \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=ON \
		-DCUDAToolkit_ROOT="$cuda_toolkit"
	cmake --build "$plugin_build_dir" --target ggml-cuda --parallel "$build_jobs"

	rm -rf "$cuda_output_dir"
	cp -a "$cpu_output_dir" "$cuda_output_dir"
	mkdir -p "$cuda_output_dir/cuda"

	local ggml_cuda_so="" candidate
	for candidate in \
		"$plugin_output_dir/libggml-cuda.so" \
		"$plugin_build_dir/libggml-cuda.so"; do
		if [ -f "$candidate" ]; then
			ggml_cuda_so="$candidate"
			break
		fi
	done
	[ -n "$ggml_cuda_so" ] || die "libggml-cuda.so not found in cuda-plugin build output."
	cp -u "$ggml_cuda_so" "$cuda_output_dir/cuda/"

	local cuda_lib_dir="$cuda_toolkit/lib64"
	[ -d "$cuda_lib_dir" ] || cuda_lib_dir="$cuda_toolkit/lib"
	cp -u "$cuda_lib_dir"/libcudart.so.* "$cuda_output_dir/cuda/" 2>/dev/null || true
	cp -u "$cuda_lib_dir"/libcublas.so.* "$cuda_output_dir/cuda/" 2>/dev/null || true
	cp -u "$cuda_lib_dir"/libcublasLt.so.* "$cuda_output_dir/cuda/" 2>/dev/null || true
	echo "    CUDA plugin build took $((SECONDS - start))s"
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

# Package a portable Linux bundle (the nix `portable` / `cuda-portable` output)
# into a versioned tar.gz. The nix output is already the flat, Windows-style
# directory (launcher + VoiceTyper.elf + .so closure + ld-linux [+ cuda/]), so
# this just copies it into a named top-level dir and tars it up.
package_portable() {
	local result_link="$1"
	local variant="$2"
	local name="VoiceTyper-v${VERSION}-${LINUX_PLATFORM}-${variant}.tar.gz"
	local top="${name%.tar.gz}"
	local stage="build/stage_${LINUX_PLATFORM}_${variant}"
	local root="$stage/$top"

	[ -f "$result_link/VoiceTyper" ] || die "portable bundle '$result_link' missing its VoiceTyper launcher."

	if [ -d "$stage" ]; then chmod -R u+w "$stage"; fi
	rm -rf "$stage"
	mkdir -p "$root"

	cp -rL "$result_link/." "$root/"
	chmod -R u+w "$root"

	# The nix output is a clean build, but strip any runtime/local files just in
	# case so the portable tarball stays pristine (no crash dumps, no
	# settings.ini - the app creates that on the user's first settings change).
	rm -f "$root"/voicetyper-crash-* "$root/settings.ini"

	tar -C "$stage" -czf "$DIST_DIR/$name" "$top"
	echo "    packaged $name"
}

# Build + package one portable variant. Runs as a background job so the cpu and
# cuda variants build in parallel (they are independent nix derivations). ccache
# is shared: enabling it for the cpu build warms the cache that the cuda build's
# CPU objects then hit, so CPU objects compile once instead of twice.
build_and_package_portable() {
	local package="$1"
	local out_link="$2"
	local variant="$3"
	local use_ccache="$4"

	local start=$SECONDS
	build_nix "$package" "$out_link" "$use_ccache"
	package_portable "$out_link" "$variant"
	echo "    $variant took $((SECONDS - start))s"
}

linux_package() {
	local want_ccache=0
	[ "$USE_CCACHE" = "1" ] && want_ccache=1
	[ -d "${CCACHE_DIR:-/var/cache/voicetyper-ccache}" ] && want_ccache=1

	# Fix permissions on existing cache subdirs from pre-CCACHE_UMASK builds so
	# parallel nixbld users can share the cache. CCACHE_UMASK=000 handles new
	# dirs, but old ones (mode 0755 owned by a single nixbld) would still block.
	if [ "$want_ccache" = "1" ]; then
		chmod -R a+rwX "${CCACHE_DIR:-/var/cache/voicetyper-ccache}" 2>/dev/null || true
	fi

	rm -rf "$DIST_DIR"
	mkdir -p "$DIST_DIR"

	echo "=== Building portable Linux packages ($LINUX_PLATFORM) for v$VERSION ==="
	echo "    (cpu + cuda build in parallel; both share ccache when enabled)"
	local start=$SECONDS
	JOB_PIDS=()
	JOB_NAMES=()
	run_job "portable (cpu)" build_and_package_portable portable result-portable cpu "$want_ccache"
	run_job "portable (cuda)" build_and_package_portable cuda-portable result-cuda-portable cuda "$want_ccache"
	wait_for_jobs
	echo "    Linux builds took $((SECONDS - start))s"

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

# Make sure $commit (the current-branch HEAD of this Windows checkout) is
# reachable on $NIX_GIT_REMOTE so the NixOS box can fetch it into the release
# tree. Fetches the remote's branch and, when it does not already contain the
# commit, pushes the commit to it - otherwise building from an unpushed HEAD
# would fail the release tree's checkout on the box.
ensure_commit_on_remote() {
	local commit="$1"
	local branch remote_tip
	branch="$(git branch --show-current 2>/dev/null || true)"
	if [ -z "$branch" ]; then
		echo "[linux] detached HEAD at $commit - assuming it is already on $NIX_GIT_REMOTE."
		return 0
	fi
	git fetch "$NIX_GIT_REMOTE" || die "Fetching $NIX_GIT_REMOTE failed; cannot sync the release commit."
	remote_tip="$(git rev-parse -q --verify "refs/remotes/$NIX_GIT_REMOTE/$branch^{commit}" || true)"
	if [ -n "$remote_tip" ] && git merge-base --is-ancestor "$commit" "$remote_tip"; then
		return 0
	fi
	echo "[linux] pushing $commit to $NIX_GIT_REMOTE/$branch so the NixOS box can fetch it..."
	git push "$NIX_GIT_REMOTE" "$commit:refs/heads/$branch"
}

# Run the portable Linux build + package on the remote NixOS box in a dedicated
# release tree ($NIX_RELEASE_REPO), force-synced to $commit before every build:
# the current branch is fetched explicitly (the commit itself is guaranteed to
# be on $REMOTE by ensure_commit_on_remote), then force-checked-out and cleaned
# of all untracked + ignored files so each release starts from pristine source.
# The dev checkout at $NIX_REPO is never touched (it only seeds a fast local
# clone to bootstrap the release tree; without it the tree clones straight from
# the git remote). ccache lives outside the tree (/var/cache/voicetyper-ccache)
# and is still shared.
# This is the part of the Linux step that can overlap the Windows build: it
# stays on the remote box and does NOT touch the local $DIST_DIR. The artifact
# stream-back is done separately, after windows_package (which clears
# $DIST_DIR), to avoid clobbering it.
linux_remote_build() {
	local commit="$1"
	local branch
	local ccache_arg=""
	local remote_url
	[ "$USE_CCACHE" = "1" ] && ccache_arg="--ccache"
	branch="$(git branch --show-current 2>/dev/null || true)"
	ensure_commit_on_remote "$commit"
	remote_url="$(git remote get-url "$NIX_GIT_REMOTE")"
	echo "Syncing release tree $NIX_RELEASE_REPO to $commit (${branch:-detached HEAD})..."
	ssh "$NIX_SSH" "set -e
		if [ ! -d $NIX_RELEASE_REPO/.git ]; then
			if [ -d $NIX_REPO/.git ]; then
				echo '[linux] bootstrapping release tree from the dev repo (local clone)'
				git clone $NIX_REPO $NIX_RELEASE_REPO
				git -C $NIX_RELEASE_REPO remote set-url origin '$remote_url'
			else
				echo \"[linux] bootstrapping release tree from $remote_url\"
				git clone '$remote_url' $NIX_RELEASE_REPO
			fi
		fi
		cd $NIX_RELEASE_REPO
		if [ -n '$branch' ]; then
			git fetch origin --tags '+refs/heads/$branch:refs/remotes/origin/$branch'
		else
			git fetch --all --tags
		fi
		git cat-file -e '$commit^{commit}' || {
			echo \"Error: commit $commit not found on $remote_url - is it pushed?\" >&2
			exit 1
		}
		git checkout --force '$commit'
		git clean -ffdx"
	echo "Building + packaging on the NixOS box..."
	ssh "$NIX_SSH" "cd $NIX_RELEASE_REPO && bash tools/release.sh --internal-linux-package $ccache_arg"
}

# Populated when a background remote Linux build is in flight; cleared once it
# has been joined. cleanup_on_exit kills it so a failure during the Windows step
# doesn't orphan a remote nix build.
LINUX_BG_PID=""

# RELEASE_SUCCESS is flipped to 1 only at the very end of a clean run so the
# EXIT trap knows to skip the (network-hitting) tag-rollback check on success.
RELEASE_SUCCESS=0

cleanup_on_exit() {
	if [ -n "$LINUX_BG_PID" ] && kill -0 "$LINUX_BG_PID" 2>/dev/null; then
		echo "Cleaning up background Linux build (pid $LINUX_BG_PID) after early exit." >&2
		kill "$LINUX_BG_PID" 2>/dev/null
		wait "$LINUX_BG_PID" 2>/dev/null
	fi
	if [ "$RELEASE_SUCCESS" = "0" ] && [ "$DO_TAG" = "1" ] && tag_exists_locally && ! tag_exists_remote; then
		echo "Cleaning up unpushed local tag $TAG due to failure." >&2
		git tag -d "$TAG" >/dev/null
	fi
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

# Local Linux dev build runs IN the nix devShell (tools/build.sh re-invokes
# this script with --internal-linux-build) to run linux_build() only.
if [ "$INTERNAL_LINUX_BUILD" = "1" ]; then
	require_command cmake
	linux_build
	exit 0
fi

# From here on we are in the main orchestration flow. Install the EXIT trap that
# rolls back an unpushed tag and/or kills an in-flight background Linux build on
# early exit. (Set after the internal dispatch blocks above so those sub-shells,
# which exit 0, don't trigger it.)
trap cleanup_on_exit EXIT

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
fi

# ---------------------------------------------------------------------------
# Pre-flight: tag / release state
# ---------------------------------------------------------------------------

if [ "$DO_TAG" = "1" ] || [ "$DO_RELEASE" = "1" ]; then
	require_clean_tree
	if [ "$DO_TAG" = "1" ]; then
		release_exists && die "GitHub release $TAG already exists; delete it from GitHub before re-cutting."
		if tag_exists_locally; then
			echo "Local tag $TAG already exists - removing."
			git tag -d "$TAG" >/dev/null
		fi
		if tag_exists_remote; then
			echo "Remote tag $TAG already exists on $REMOTE - removing."
			git push "$REMOTE" --delete "$TAG" >/dev/null
		fi
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
fi

# ---------------------------------------------------------------------------
# Step 2: build + package Windows
# ---------------------------------------------------------------------------

TOTAL_START=$SECONDS

# If the remote Linux build is requested, kick it off NOW (after the tag is cut)
# so the long remote nix build overlaps the Windows build+package. Only the sync
# + remote build run in the background - they stay on the NixOS box and don't
# touch the local $DIST_DIR. The artifact stream-back is deferred until after
# windows_package (which clears $DIST_DIR) so it can't be clobbered.
if [ "$DO_LINUX" = "1" ]; then
	echo "=== Starting remote Linux build (background, overlaps Windows) ==="
	linux_remote_build "$(git rev-parse HEAD)" &
	LINUX_BG_PID=$!
fi

windows_build
echo ""
windows_package

# ---------------------------------------------------------------------------
# Step 3: join the background Linux build (if any) and stream artifacts back
# ---------------------------------------------------------------------------

if [ "$DO_LINUX" = "1" ]; then
	echo ""
	echo "=== Joining remote Linux build ==="
	linux_start=$SECONDS
	if wait "$LINUX_BG_PID"; then
		LINUX_BG_PID=""
		echo "Streaming Linux artifacts back into local $DIST_DIR/..."
		ssh "$NIX_SSH" "cd $NIX_RELEASE_REPO && tar -cf - -C $DIST_DIR ." | tar -xf - -C "$DIST_DIR"
		echo "    Linux join + stream took $((SECONDS - linux_start))s"
	else
		LINUX_BG_PID=""
		die "Remote Linux build failed."
	fi
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
	echo "=== Tag $TAG pushed ==="
fi

RELEASE_SUCCESS=1
echo ""
echo "=== Done in $((SECONDS - TOTAL_START))s ==="
echo "Created:"
ls -1 "$DIST_DIR" 2>/dev/null || true
