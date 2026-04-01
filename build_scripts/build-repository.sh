#!/bin/bash
# build-repository.sh — Assemble a versioned package repository tree.
#
# Usage: scripts/build-repository.sh \
#          <release-dir> <repo-dir> <target-root> <repo-slug>
#
# Replaces <repo-dir>/<target-root>/ with the structured package tree from
# <release-dir>, following the layout described in docs/repository-layout.md.
#
# <release-dir>       Directory containing the structured package tree
# <repo-dir>          Root of the repository tree (git worktree or plain directory)
# <target-root>       Destination root inside the repository branch,
#                     e.g. "stable" or "feature_unify_packaging"
# <repo-slug>         GitHub repository slug, e.g. "owner/repo"

set -euo pipefail

RELEASE_DIR="${1:?Usage: $0 <release-dir> <repo-dir> <target-root> <repo-slug>}"
REPO_DIR="${2:?}"
TARGET_ROOT_INPUT="${3:?}"
REPO_SLUG="${4:?}"

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

RAW_BASE_URL="https://raw.githubusercontent.com/$REPO_SLUG/repository/$TARGET_ROOT"
INSTRUCTIONS_FILE="$ROOT_DIR/README.md"

{
    echo "# keen-pbr package repository"
    echo
    echo "Repository root for \`$TARGET_ROOT\`."
    echo
    echo "Base URL:"
    echo
    echo "\`$RAW_BASE_URL\`"
    echo

    echo "## OpenWrt"
    if find "$ROOT_DIR/openwrt" -mindepth 2 -maxdepth 2 -type d >/dev/null 2>&1; then
        echo
        echo "Open `/etc/opkg/customfeeds.conf` and add one of these lines:"
        echo
        find "$ROOT_DIR/openwrt" -mindepth 2 -maxdepth 2 -type d | sort | while read -r arch_dir; do
            rel_path="${arch_dir#$ROOT_DIR/}"
            version_dir="$(basename "$(dirname "$arch_dir")")"
            arch_name="$(basename "$arch_dir")"
            printf -- "- `%s`: `src/gz keen-pbr %s/%s`\n" "$version_dir / $arch_name" "$RAW_BASE_URL" "$rel_path"
        done
        echo
        echo "Then run:"
        echo
        echo '```sh'
        echo 'opkg update'
        echo 'opkg install keen-pbr'
        echo '```'
    else
        echo
        echo "No OpenWrt packages published in this target."
    fi

    echo
    echo "## Keenetic Entware"
    if find "$ROOT_DIR/keenetic" -mindepth 2 -maxdepth 2 -type d >/dev/null 2>&1; then
        echo
        echo "Open `/opt/etc/opkg/customfeeds.conf` and add one of these lines:"
        echo
        find "$ROOT_DIR/keenetic" -mindepth 2 -maxdepth 2 -type d | sort | while read -r arch_dir; do
            rel_path="${arch_dir#$ROOT_DIR/}"
            version_dir="$(basename "$(dirname "$arch_dir")")"
            arch_name="$(basename "$arch_dir")"
            printf -- "- `%s / %s`: `src/gz keen-pbr %s/%s`\n" "$version_dir" "$arch_name" "$RAW_BASE_URL" "$rel_path"
        done
        echo
        echo "Then run:"
        echo
        echo '```sh'
        echo 'opkg update'
        echo 'opkg install keen-pbr'
        echo '```'
    else
        echo
        echo "No Keenetic Entware packages published in this target."
    fi

    echo
    echo "## Debian"
    if find "$ROOT_DIR/debian" -mindepth 2 -maxdepth 2 -type d >/dev/null 2>&1; then
        echo
        echo "Open `/etc/apt/sources.list.d/keen-pbr.list` and add one of these lines:"
        echo
        find "$ROOT_DIR/debian" -mindepth 2 -maxdepth 2 -type d | sort | while read -r arch_dir; do
            rel_path="${arch_dir#$ROOT_DIR/}"
            version_dir="$(basename "$(dirname "$arch_dir")")"
            arch_name="$(basename "$arch_dir")"
            printf -- "- `%s / %s`: `deb [trusted=yes] %s/%s ./`\n" "$version_dir" "$arch_name" "$RAW_BASE_URL" "$rel_path"
        done
        echo
        echo "Then run:"
        echo
        echo '```sh'
        echo 'apt update'
        echo 'apt install keen-pbr'
        echo '```'
    else
        echo
        echo "No Debian packages published in this target."
    fi
} > "$INSTRUCTIONS_FILE"

echo "[build-repository] Repository tree written to $ROOT_DIR"
