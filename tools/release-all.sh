#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

VERSION="$(tr -d '[:space:]' < VERSION)"
TAG="v$VERSION"
DIST_DIR="dist"
REMOTE="${VOICETYPER_RELEASE_REMOTE:-origin}"
NIX_SSH="${VOICETYPER_NIX_SSH:-rock}"
NIX_REPO="${VOICETYPER_NIX_REPO:-~/repos/VoiceTyper}"
SKIP_WINDOWS=0
SKIP_LINUX=0
DRAFT=1
PRERELEASE=0
CHANGELOG_FILE="$DIST_DIR/release-notes-$TAG.md"

usage() {
	cat <<EOF
Usage: tools/release-all.sh [options]

Coordinates Windows (local) and Linux (remote NixOS over ssh) builds, packages
the Windows cpu/cuda outputs plus the portable Linux outputs (static executable
+ AppImage), and creates a single GitHub release with every artifact.

Options:
  --draft            Create the GitHub release as a draft (default).
  --no-draft         Publish the GitHub release immediately.
  --prerelease       Mark the GitHub release as a prerelease.
  --skip-windows     Skip the local Windows build/package step.
  --skip-linux       Skip the remote Linux build/package step.
  --remote NAME      Git remote to push the release tag to (default: origin).
  --nix-ssh TARGET   ssh target for the NixOS box (env: VOICETYPER_NIX_SSH, default: rock).
  --nix-repo PATH    Repo path on the NixOS box (env: VOICETYPER_NIX_REPO, default: ~/repos/VoiceTyper).
  -h|--help          Show this help.

Runs from a Windows host in git bash. The Windows cpu/cuda builds are produced
locally; the portable Linux outputs (static executable + AppImage) are produced
by ssh-ing into the NixOS box and running tools/nix-package.sh.
EOF
}

die() {
	echo "Error: $*" >&2
	exit 1
}

require_command() {
	if ! command -v "$1" >/dev/null 2>&1; then
		die "Required command '$1' was not found."
	fi
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--draft) DRAFT=1 ;;
		--no-draft) DRAFT=0 ;;
		--prerelease) PRERELEASE=1 ;;
		--skip-windows) SKIP_WINDOWS=1 ;;
		--skip-linux) SKIP_LINUX=1 ;;
		--remote) [ "$#" -ge 2 ] || die "--remote requires a name."; REMOTE="$2"; shift ;;
		--nix-ssh) [ "$#" -ge 2 ] || die "--nix-ssh requires a target."; NIX_SSH="$2"; shift ;;
		--nix-repo) [ "$#" -ge 2 ] || die "--nix-repo requires a path."; NIX_REPO="$2"; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "Unknown option '$1'." ;;
	esac
	shift
done

[ -n "$VERSION" ] || die "VERSION is empty."

require_command gh
if [ "$SKIP_LINUX" -eq 0 ]; then
	require_command ssh
	require_command scp
fi

# The VAD model file is .gitignore'd so it doesn't travel with git checkout on
# the NixOS box — it needs to be staged explicitly. STT models are no longer
# shipped in dist artifacts (the in-app downloader fetches them on demand from
# huggingface.co/ggerganov/whisper.cpp).
for f in vad_models/ggml-silero-v5.1.2.bin; do
	[ -f "$f" ] || die "Model file '$f' not found (needed by Linux portable bundles)."
done

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
		die "Commit or stash tracked changes before releasing."
	fi
}

echo "=== Cutting $TAG ==="

require_clean_tree
if tag_exists_locally; then die "Local tag $TAG already exists."; fi
if tag_exists_remote; then die "Remote tag $TAG already exists on $REMOTE."; fi
if release_exists; then die "GitHub release $TAG already exists."; fi

COMMIT="$(git rev-parse HEAD)"

echo "Tagging HEAD ($COMMIT) so builds embed the clean version string..."
git tag -a "$TAG" -m "VoiceTyper $TAG"

cleanup_tag() {
	if tag_exists_locally && ! tag_exists_remote; then
		echo "Cleaning up unpushed local tag $TAG due to failure." >&2
		git tag -d "$TAG" >/dev/null
	fi
}
trap cleanup_tag EXIT

if [ "$SKIP_WINDOWS" -eq 0 ]; then
	echo ""
	echo "=== Windows build + package (local) ==="
	START=$SECONDS
	tools/win-package.sh build
	echo "    Windows step took $((SECONDS - START))s"
else
	echo "Skipping Windows build (--skip-windows)."
fi

if [ "$SKIP_LINUX" -eq 0 ]; then
	echo ""
	echo "=== Linux build + package (remote $NIX_SSH:$NIX_REPO) ==="
	START=$SECONDS

	echo "Syncing remote tree to $COMMIT..."
	ssh "$NIX_SSH" "cd $NIX_REPO && git fetch --all --tags && git checkout '$COMMIT'"

	echo "Staging VAD model file to the NixOS box..."
	tar -cf - vad_models/ggml-silero-v5.1.2.bin \
		| ssh "$NIX_SSH" "cd $NIX_REPO && tar -xf -"

	echo "Building + packaging on the NixOS box..."
	ssh "$NIX_SSH" "cd $NIX_REPO && bash tools/nix-package.sh"

	echo "Streaming Linux artifacts back into local $DIST_DIR/..."
	ssh "$NIX_SSH" "cd $NIX_REPO && tar -cf - -C $DIST_DIR ." | tar -xf - -C "$DIST_DIR"

	echo "    Linux step took $((SECONDS - START))s"
else
	echo "Skipping Linux build (--skip-linux)."
fi

echo ""
echo "=== Generating release notes ==="
PREVIOUS="$(git tag --sort=-v:refname | grep -vxF "$TAG" | head -n 1 || true)"
if [ -n "$PREVIOUS" ]; then
	RANGE="$PREVIOUS..HEAD"
else
	RANGE="HEAD"
fi
{
	printf "# VoiceTyper %s\n\n" "$TAG"
	if [ -n "$PREVIOUS" ]; then printf "Changes since %s:\n\n" "$PREVIOUS"; else printf "Changes:\n\n"; fi
	git log "$RANGE" --pretty=format:'- %s (%h)'
	printf "\n"
} > "$CHANGELOG_FILE"

echo ""
echo "=== Verifying dist artifacts ==="
shopt -s nullglob
ARTIFACTS=()
for file in "$DIST_DIR"/*; do
	if [ ! -f "$file" ]; then continue; fi
	case "$file" in *.md) continue ;; esac
	basename="$(basename "$file")"
	case "$basename" in *-v${VERSION}-*) ;; *) die "Artifact '$basename' does not match version $VERSION." ;; esac
	ARTIFACTS+=("$file")
done
shopt -u nullglob

if [ "${#ARTIFACTS[@]}" -eq 0 ]; then
	die "No package artifacts found in $DIST_DIR."
fi

echo "Found ${#ARTIFACTS[@]} artifact(s) for $TAG:"
for asset in "${ARTIFACTS[@]}"; do
	echo "    $(basename "$asset")"
done

echo ""
echo "Pushing tag to $REMOTE..."
git push "$REMOTE" "$TAG"
trap - EXIT

echo "Creating GitHub release..."
RELEASE_ARGS=("$TAG" "${ARTIFACTS[@]}" --title "VoiceTyper $TAG" --notes-file "$CHANGELOG_FILE" --verify-tag)
if [ "$DRAFT" -eq 1 ]; then RELEASE_ARGS+=(--draft); fi
if [ "$PRERELEASE" -eq 1 ]; then RELEASE_ARGS+=(--prerelease); fi
gh release create "${RELEASE_ARGS[@]}"

echo ""
echo "=== $TAG cut successfully ==="
