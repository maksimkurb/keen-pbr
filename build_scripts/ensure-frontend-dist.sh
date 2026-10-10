#!/bin/sh

set -eu

WORKSPACE="${1:?Usage: $0 <workspace-dir> <dist-dir> <platform>}"
DIST_DIR="${2:-$WORKSPACE/frontend/dist}"
PLATFORM="${3:?Usage: $0 <workspace-dir> <dist-dir> <platform>}"

# Reuse an existing bundle (e.g. a CI artifact) only when it was built for the
# same platform; the platform is baked into the bundle at build time.
if [ -f "$DIST_DIR/.keen-pbr-platform" ] &&
    [ "$(cat "$DIST_DIR/.keen-pbr-platform")" = "$PLATFORM" ]; then
    exit 0
fi

sh "$WORKSPACE/build_scripts/build-frontend.sh" "$WORKSPACE" "$DIST_DIR" "$PLATFORM"
