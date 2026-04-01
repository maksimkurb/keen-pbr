#!/bin/bash
# collect-debian.sh — Normalize Debian package names and generate repository metadata.
#
# Usage: scripts/collect-debian.sh <release-dir> [debian-version]
#
# Expects <release-dir> to contain *.deb files produced by dpkg-buildpackage.
# Renames them to <pkg>_<version>_debian_<arch>.deb and writes them into the
# repository-style tree: debian/<debian-version>/<arch>/ with Packages,
# Packages.gz, and Release files alongside the .deb files.
#
# Requires: dpkg-scanpackages, apt-ftparchive (from dpkg-dev and apt-utils).

set -euo pipefail

RELEASE_DIR="${1:?Usage: $0 <release-dir> [debian-version]}"
DEBIAN_VERSION="${2:-bookworm}"

mkdir -p "$RELEASE_DIR"

# ── Normalize filenames ───────────────────────────────────────────────────────

find "$RELEASE_DIR" -maxdepth 1 -type f -name '*.deb' | while read -r f; do
    BASENAME=$(basename "$f")
    PKG_NAME="${BASENAME%%_*}"
    VERSION_RELEASE=$(printf '%s\n' "$BASENAME" | sed -E 's/^[^_]+_([^_]+)_.+$/\1/')
    DPKG_ARCH=$(printf '%s\n' "$BASENAME" | sed -E 's/^[^_]+_[^_]+_([^.]+)\.deb$/\1/')
    DEST_DIR="$RELEASE_DIR/debian/${DEBIAN_VERSION}/${DPKG_ARCH}"
    mkdir -p "$DEST_DIR"
    cp "$f" "$DEST_DIR/${PKG_NAME}_${VERSION_RELEASE}_debian_${DPKG_ARCH}.deb"
done

find "$RELEASE_DIR" -maxdepth 1 -type f -name '*.deb' -delete

# ── Generate per-arch Packages + Release ─────────────────────────────────────

for ARCH_DIR in $(find "$RELEASE_DIR/debian/${DEBIAN_VERSION}" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort); do
    DPKG_ARCH=$(basename "$ARCH_DIR")
    TMP_IDX=$(mktemp -d)
    find "$ARCH_DIR" -maxdepth 1 -type f -name "*_debian_${DPKG_ARCH}.deb" -exec cp {} "$TMP_IDX/" \;
    (
        cd "$TMP_IDX"
        dpkg-scanpackages --multiversion . /dev/null > Packages
        gzip -n9c Packages > Packages.gz
        apt-ftparchive release . > Release
    )
    cp "$TMP_IDX/Packages"    "$ARCH_DIR/Packages"
    cp "$TMP_IDX/Packages.gz" "$ARCH_DIR/Packages.gz"
    cp "$TMP_IDX/Release"     "$ARCH_DIR/Release"
    rm -rf "$TMP_IDX"
done
