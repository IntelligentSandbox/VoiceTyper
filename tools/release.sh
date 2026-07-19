#!/bin/bash

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

VERSION="$(tr -d '[:space:]' < VERSION)"
TAG="v$VERSION"
DIST_DIR="dist"
REMOTE="${VOICETYPER_RELEASE_REMOTE:-origin}"
CHANGELOG_FILE="$DIST_DIR/release-notes-$TAG.md"

usage() {
	cat <<EOF
Usage:
  tools/release.sh changelog [--git|--github] [--file PATH]
  tools/release.sh cut [--draft] [--prerelease] [--file PATH] [--remote NAME]
  tools/release.sh push [--draft] [--prerelease] [--file PATH] [--remote NAME]

Actions:
  changelog   Regenerate editable release notes for $TAG.
  cut         Tag HEAD, run a clean build + package, generate release notes if
              missing, verify dist artifacts, then create and push the $TAG git
              tag and GitHub release. Pre-run 'changelog' to edit notes first.
  push        Create and push the $TAG git tag, then create the GitHub release.
              Assumes build/package artifacts are already present in $DIST_DIR.

Options:
  --git         Build release notes from local git commits since the previous tag. This is the default.
  --github      Ask GitHub to generate release notes and save them for review.
  --file PATH   Use a custom release notes file.
  --remote NAME Push the release tag to a custom git remote. The default is origin.
  --draft       Create the GitHub release as a draft.
  --prerelease  Mark the GitHub release as a prerelease.
EOF
}

die() {
	echo "Error: $*" >&2
	exit 1
}

require_command() {
	if command -v "$1" >/dev/null 2>&1; then
		return
	fi

	die "Required command '$1' was not found."
}

previous_tag() {
	git tag --sort=-v:refname | grep -vxF "$TAG" | head -n 1 || true
}

generate_git_changelog() {
	local previous
	local commits
	local range

	previous="$(previous_tag)"

	if [ -n "$previous" ]; then
		range="$previous..HEAD"
	else
		range="HEAD"
	fi

	commits="$(git log "$range" --pretty=format:'- %s (%h)')"

	{
		printf "# VoiceTyper %s\n\n" "$TAG"
		if [ -n "$previous" ]; then
			printf "Changes since %s:\n\n" "$previous"
		else
			printf "Changes:\n\n"
		fi

		if [ -n "$commits" ]; then
			printf "%s\n" "$commits"
		else
			printf "_No commits found._\n"
		fi
	} > "$CHANGELOG_FILE"
}

generate_github_changelog() {
	local previous
	local target_commit
	local api_args

	require_command gh

	previous="$(previous_tag)"
	target_commit="$(git rev-parse HEAD)"
	api_args=(-f "tag_name=$TAG" -f "target_commitish=$target_commit")

	if [ -n "$previous" ]; then
		api_args+=(-f "previous_tag_name=$previous")
	fi

	{
		printf "# VoiceTyper %s\n\n" "$TAG"
		gh api 'repos/{owner}/{repo}/releases/generate-notes' "${api_args[@]}" --jq '.body'
		printf "\n"
	} > "$CHANGELOG_FILE"
}

run_changelog() {
	local source

	source="git"

	while [ "$#" -gt 0 ]; do
		case "$1" in
			--git)
				source="git"
				;;
			--github)
				source="github"
				;;
			--file)
				[ "$#" -ge 2 ] || die "--file requires a path."
				CHANGELOG_FILE="$2"
				shift
				;;
			-h|--help)
				usage
				exit 0
				;;
			*)
				die "Unknown changelog option '$1'."
				;;
		esac
		shift
	done

	mkdir -p "$(dirname "$CHANGELOG_FILE")"

	case "$source" in
		git)
			generate_git_changelog
			;;
		github)
			generate_github_changelog
			;;
	esac

	echo "Wrote $CHANGELOG_FILE"
}

require_clean_tracked_files() {
	if [ -z "$(git status --porcelain --untracked-files=no)" ]; then
		return
	fi

	git status --short
	die "Commit or stash tracked changes before releasing."
}

release_exists() {
	gh release view "$TAG" >/dev/null 2>&1
}

require_new_tag_and_release() {
	if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
		die "Local tag $TAG already exists."
	fi

	if git ls-remote --exit-code --tags "$REMOTE" "refs/tags/$TAG" >/dev/null 2>&1; then
		die "Remote tag $TAG already exists on $REMOTE."
	fi

	if release_exists; then
		die "GitHub release $TAG already exists."
	fi
}

collect_release_assets() {
	local files
	local file
	local basename

	shopt -s nullglob
	files=("$DIST_DIR"/*)
	shopt -u nullglob

	RELEASE_ASSETS=()

	for file in "${files[@]}"; do
		if [ ! -f "$file" ]; then
			continue
		fi

		if [ -f "$CHANGELOG_FILE" ] && [ "$file" -ef "$CHANGELOG_FILE" ]; then
			continue
		fi

		if [[ "$file" == "$DIST_DIR"/*.md ]]; then
			continue
		fi

		basename="$(basename "$file")"
		if [[ "$basename" != *"-v${VERSION}-"* ]]; then
			die "Dist artifact '$basename' does not match version $VERSION. Remove stale $DIST_DIR contents and re-run 'tools/package.sh'."
		fi

		RELEASE_ASSETS+=("$file")
	done

	if [ ! -f "$CHANGELOG_FILE" ]; then
		die "Release notes file '$CHANGELOG_FILE' does not exist. Run 'tools/release.sh changelog' first."
	fi

	if [ "${#RELEASE_ASSETS[@]}" -eq 0 ]; then
		die "No package artifacts found in $DIST_DIR. Run 'tools/package.sh' first."
	fi
}

create_github_release() {
	local draft="$1"
	local prerelease="$2"
	local release_args

	release_args=(
		"$TAG"
		"${RELEASE_ASSETS[@]}"
		--title "VoiceTyper $TAG"
		--notes-file "$CHANGELOG_FILE"
		--verify-tag
	)

	if [ "$draft" = "1" ]; then
		release_args+=(--draft)
	fi

	if [ "$prerelease" = "1" ]; then
		release_args+=(--prerelease)
	fi

	gh release create "${release_args[@]}"
}

parse_release_flags() {
	# Shared --draft/--prerelease/--file/--remote parsing for push and cut.
	local action="$1"
	shift

	DRAFT=0
	PRERELEASE=0

	while [ "$#" -gt 0 ]; do
		case "$1" in
			--draft)
				DRAFT=1
				;;
			--prerelease)
				PRERELEASE=1
				;;
			--file)
				[ "$#" -ge 2 ] || die "--file requires a path."
				CHANGELOG_FILE="$2"
				shift
				;;
			--remote)
				[ "$#" -ge 2 ] || die "--remote requires a name."
				REMOTE="$2"
				shift
				;;
			-h|--help)
				usage
				exit 0
				;;
			*)
				die "Unknown $action option '$1'."
				;;
		esac
		shift
	done
}

run_push() {
	parse_release_flags push "$@"

	require_command gh
	require_clean_tracked_files
	require_new_tag_and_release
	collect_release_assets

	git tag -a "$TAG" -m "VoiceTyper $TAG"
	git push "$REMOTE" "$TAG"

	create_github_release "$DRAFT" "$PRERELEASE"
}

run_cut() {
	parse_release_flags cut "$@"

	require_command gh
	require_command 7z
	require_clean_tracked_files
	require_new_tag_and_release

	echo "=== Cutting $TAG ==="
	echo "Tagging HEAD so the build embeds the clean version string..."
	git tag -a "$TAG" -m "VoiceTyper $TAG"

	# If anything below fails before the tag is pushed, remove the local tag so the
	# user can re-run without hitting "tag already exists".
	trap '
		if git rev-parse -q --verify "refs/tags/'"$TAG"'" >/dev/null && \
			! git ls-remote --exit-code --tags "'"$REMOTE"'" "refs/tags/'"$TAG"'" >/dev/null 2>&1; then
			echo "Cleaning up unpushed local tag '"$TAG"' due to failure." >&2
			git tag -d "'"$TAG"'" >/dev/null
		fi
	' EXIT

	echo "Running clean build + package..."
	tools/package.sh build

	if [ ! -f "$CHANGELOG_FILE" ]; then
		echo "Generating release notes (git-based)..."
		mkdir -p "$(dirname "$CHANGELOG_FILE")"
		generate_git_changelog
	else
		echo "Using existing release notes at $CHANGELOG_FILE"
	fi

	echo "Verifying dist artifacts for version $VERSION..."
	collect_release_assets

	echo "Found ${#RELEASE_ASSETS[@]} artifact(s) for $TAG:"
	local asset
	for asset in "${RELEASE_ASSETS[@]}"; do
		echo "    $(basename "$asset")"
	done

	echo "Pushing tag to $REMOTE..."
	git push "$REMOTE" "$TAG"
	trap - EXIT

	echo "Creating GitHub release..."
	create_github_release "$DRAFT" "$PRERELEASE"

	echo "=== $TAG cut successfully ==="
}

if [ -z "$VERSION" ]; then
	die "VERSION is empty."
fi

case "${1:-}" in
	changelog)
		shift
		run_changelog "$@"
		;;
	cut)
		shift
		run_cut "$@"
		;;
	push)
		shift
		run_push "$@"
		;;
	-h|--help|"")
		usage
		;;
	*)
		die "Unknown action '$1'."
		;;
esac
