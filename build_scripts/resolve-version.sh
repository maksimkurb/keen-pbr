#!/usr/bin/env bash

set -euo pipefail

MODE="${1:?Usage: $0 <version|release|full> <workspace-dir>}"
WORKSPACE="${2:?}"

. "$WORKSPACE/version.mk"

resolve_release() {
    if [ -n "${KEEN_PBR_RELEASE_OVERRIDE:-}" ]; then
        printf '%s' "$KEEN_PBR_RELEASE_OVERRIDE"
        return
    fi

    if command -v git >/dev/null 2>&1 && git -C "$WORKSPACE" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        TZ=UTC git -C "$WORKSPACE" log -1 --format=%cd --date=format-local:%Y%m%d%H%M%S
        return
    fi

    printf '%s' "$KEEN_PBR_RELEASE"
}

case "$MODE" in
    version)
        printf '%s' "$KEEN_PBR_VERSION"
        ;;
    release)
        resolve_release
        ;;
    full)
        printf '%s-%s' "$KEEN_PBR_VERSION" "$(resolve_release)"
        ;;
    *)
        echo "Unknown mode: $MODE" >&2
        exit 1
        ;;
esac
