#!/bin/bash
# build-repository.sh — Assemble a versioned package repository tree.
#
# Usage: scripts/build-repository.sh \
#          <release-dir> <repo-dir> <prefix> <keenetic-version> <debian-version>
#
# Copies the structured package tree from <release-dir> into
# <repo-dir>/<prefix>/ following the layout described in docs/repository-layout.md.
#
# <release-dir>       Directory containing the structured package tree
# <repo-dir>          Root of the repository tree (git worktree or plain directory)
# <prefix>            Channel prefix, e.g. "stable", "unstable", "dev/my-branch"
# <keenetic-version>  Keenetic channel version, e.g. "current"
# <debian-version>    Debian distribution name, e.g. "bookworm"

set -euo pipefail

RELEASE_DIR="${1:?Usage: $0 <release-dir> <repo-dir> <prefix> <keenetic-version> <debian-version>}"
REPO_DIR="${2:?}"
REPOSITORY_PREFIX="${3:?}"
KEENETIC_VERSION="${4:?}"
DEBIAN_VERSION="${5:?}"

sanitize_segment() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | sed -E 's#[^a-z0-9._-]+#-#g; s#-+#-#g; s#(^-+|-+$)##g'
}

sanitize_path() {
    local raw="$1"
    local out=""
    local IFS='/'
    read -ra parts <<< "$raw"
    for part in "${parts[@]}"; do
        [ -z "$part" ] && continue
        part=$(sanitize_segment "$part")
        [ -z "$part" ] && continue
        if [ -z "$out" ]; then
            out="$part"
        else
            out="$out/$part"
        fi
    done
    printf '%s' "$out"
}

PREFIX=$(sanitize_path "$REPOSITORY_PREFIX")
if [ -z "$PREFIX" ]; then
    echo "[build-repository] ERROR: repository prefix resolved to an empty path" >&2
    exit 1
fi

ROOT_DIR="$REPO_DIR/$PREFIX"
mkdir -p "$ROOT_DIR"

for platform in openwrt keenetic debian; do
    if [ -d "$RELEASE_DIR/$platform" ]; then
        mkdir -p "$ROOT_DIR/$platform"
        cp -a "$RELEASE_DIR/$platform"/. "$ROOT_DIR/$platform"/
    fi
done

echo "[build-repository] Repository tree written to $ROOT_DIR"
