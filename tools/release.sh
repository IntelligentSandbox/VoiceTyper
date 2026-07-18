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
  tools/release.sh push [--draft] [--prerelease] [--file PATH] [--remote NAME]

Actions:
  changelog   Regenerate editable release notes for $TAG.
  push        Create and push the $TAG git tag, then create the GitHub release.

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
	local has_package

	shopt -s nullglob
	files=("$DIST_DIR"/*)
	shopt -u nullglob

	RELEASE_ASSETS=()
	has_package=0

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

		RELEASE_ASSETS+=("$file")
		has_package=1
	done

	if [ ! -f "$CHANGELOG_FILE" ]; then
		die "Release notes file '$CHANGELOG_FILE' does not exist. Run 'tools/release.sh changelog' first."
	fi

	if [ "${#RELEASE_ASSETS[@]}" -eq 0 ]; then
		die "No release assets found."
	fi

	if [ "$has_package" = "0" ]; then
		die "No package artifacts found in $DIST_DIR. Run 'tools/package.sh' first."
	fi
}

run_push() {
	local draft
	local prerelease
	local release_args

	draft=0
	prerelease=0

	while [ "$#" -gt 0 ]; do
		case "$1" in
			--draft)
				draft=1
				;;
			--prerelease)
				prerelease=1
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
				die "Unknown push option '$1'."
				;;
		esac
		shift
	done

	require_command gh
	require_clean_tracked_files
	require_new_tag_and_release
	collect_release_assets

	git tag -a "$TAG" -m "VoiceTyper $TAG"
	git push "$REMOTE" "$TAG"

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

if [ -z "$VERSION" ]; then
	die "VERSION is empty."
fi

case "${1:-}" in
	changelog)
		shift
		run_changelog "$@"
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
