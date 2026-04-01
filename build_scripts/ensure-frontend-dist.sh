#!/bin/sh

set -eu

WORKSPACE="${1:?Usage: $0 <workspace-dir> [dist-dir]}"
DIST_DIR="${2:-$WORKSPACE/frontend/dist}"

if [ -d "$DIST_DIR" ] && find "$DIST_DIR" -mindepth 1 -print -quit >/dev/null 2>&1; then
    exit 0
fi

sh "$WORKSPACE/build_scripts/build-frontend.sh" "$WORKSPACE" "$DIST_DIR"
