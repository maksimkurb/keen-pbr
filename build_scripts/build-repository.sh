#!/bin/bash
# build-repository.sh — Assemble a versioned package repository tree.
#
# Usage: scripts/build-repository.sh \
#          <release-dir> <repo-dir> <target-root>
#
# Replaces <repo-dir>/<target-root>/ with the structured package tree from
# <release-dir>, following the layout described in docs/repository-layout.md.

set -euo pipefail

RELEASE_DIR="${1:?Usage: $0 <release-dir> <repo-dir> <target-root>}"
REPO_DIR="${2:?}"
TARGET_ROOT_INPUT="${3:?}"
REPO_PUBLIC_BASE_URL="${REPO_PUBLIC_BASE_URL:-https://repo.keen-pbr.fyi}"
REPO_SOURCE_REF_TYPE="${REPO_SOURCE_REF_TYPE:-branch}"
REPO_SOURCE_REF_NAME="${REPO_SOURCE_REF_NAME:-$TARGET_ROOT_INPUT}"
REPO_SOURCE_PR_NUMBER="${REPO_SOURCE_PR_NUMBER:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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

TARGET_ROOT=$(sanitize_path "$TARGET_ROOT_INPUT")
if [ -z "$TARGET_ROOT" ]; then
    echo "[build-repository] ERROR: target root resolved to an empty path" >&2
    exit 1
fi

ROOT_DIR="$REPO_DIR/$TARGET_ROOT"
rm -rf "$ROOT_DIR"
mkdir -p "$ROOT_DIR"

for platform in openwrt keenetic debian; do
    if [ -d "$RELEASE_DIR/$platform" ]; then
        mkdir -p "$ROOT_DIR/$platform"
        cp -a "$RELEASE_DIR/$platform"/. "$ROOT_DIR/$platform"/
    fi
done

find "$ROOT_DIR" -type f \( -name '.apk-signed' -o -name '.signed' \) -delete

generator_args=(
    --root-dir "$ROOT_DIR"
    --repo-dir "$REPO_DIR"
    --target-root "$TARGET_ROOT"
    --public-base-url "$REPO_PUBLIC_BASE_URL"
    --source-ref-type "$REPO_SOURCE_REF_TYPE"
    --source-ref-name "$REPO_SOURCE_REF_NAME"
    --shared-assets-source "$SCRIPT_DIR/../repo/assets"
    --shared-keys-source "$SCRIPT_DIR/../repo/keys"
)

if [ -n "$REPO_SOURCE_PR_NUMBER" ]; then
    generator_args+=(--source-pr-number "$REPO_SOURCE_PR_NUMBER")
fi

python3 "$SCRIPT_DIR/generate_repository_page.py" "${generator_args[@]}"

echo "[build-repository] Repository tree written to $ROOT_DIR"
