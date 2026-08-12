#!/usr/bin/env bash
#
# tools/build.sh - Linux dev iteration wrapper (mirror of tools/build.bat).
#
# Meant to be run from inside the nix devShell (`nix develop`). Re-invokes
# tools/release.sh --internal-linux-build, which runs a plain cmake build
# (CPU, plus the CUDA plugin when a CUDA toolkit is available) and stages the
# runnable outputs under build/Release_cpu [and build/Release_cuda].
#
# Usage:
#   nix develop
#   tools/build.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

exec bash "$SCRIPT_DIR/release.sh" --internal-linux-build "$@"
