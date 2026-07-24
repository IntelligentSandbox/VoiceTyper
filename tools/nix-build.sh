#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

PACKAGE="default"
USE_CCACHE=0
CCACHE_DIR_DEFAULT="/var/cache/voicetyper-ccache"

usage() {
	cat <<EOF
Usage: tools/nix-build.sh [package] [--ccache] [-- <nix-args>...]
  package    default | cuda | static | appimage   (default: default)
  --ccache   enable ccache (cuda package only). Requires a ccache dir that
             nixbld can write (default /var/cache/voicetyper-ccache, or \$CCACHE_DIR).
             The sandbox hole is applied for this build only.

Examples:
  tools/nix-build.sh
  tools/nix-build.sh cuda --ccache
  tools/nix-build.sh cuda --ccache -- --print-build-logs
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--ccache) USE_CCACHE=1 ;;
		-h|--help) usage; exit 0 ;;
		--) shift; break ;;
		-*) echo "unknown option: $1" >&2; usage; exit 1 ;;
		*) PACKAGE="$1" ;;
	esac
	shift
done

NIX_ARGS=("$@")

if [ "$USE_CCACHE" = "1" ]; then
	if [ "$PACKAGE" != "cuda" ]; then
		echo "ccache is only wired into the cuda package." >&2
		exit 1
	fi
	CCACHE_DIR="${CCACHE_DIR:-$CCACHE_DIR_DEFAULT}"
	if [ ! -d "$CCACHE_DIR" ]; then
		echo "ccache dir $CCACHE_DIR does not exist." >&2
		echo "create it: sudo mkdir -p $CCACHE_DIR && sudo chmod 1777 $CCACHE_DIR" >&2
		exit 1
	fi
	export VOICETYPER_CCACHE=1
	export CCACHE_DIR
	NIX_ARGS+=("--impure" "--option" "extra-sandbox-paths" "$CCACHE_DIR")
	echo "[nix-build] ccache on -> $CCACHE_DIR"
fi

echo "[nix-build] building .#$PACKAGE"
exec nix build ".#$PACKAGE" "${NIX_ARGS[@]}"
