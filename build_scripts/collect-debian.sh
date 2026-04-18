#!/bin/bash
# collect-debian.sh — Normalize Debian package names and generate repository metadata.
#
# Usage: scripts/collect-debian.sh <release-dir> [debian-version]
#
# Expects <release-dir> to contain *.deb and optional *.ddeb files produced by
# dpkg-buildpackage. Renames them to <pkg>_<version>_debian_<arch>.<ext> and
# writes them into the repository-style tree: debian/<debian-version>/<arch>/
# with Packages, Packages.gz, and Release files alongside the package files.
#
# Requires: dpkg-scanpackages, apt-ftparchive (from dpkg-dev and apt-utils).

set -euo pipefail

RELEASE_DIR="${1:?Usage: $0 <release-dir> [debian-version]}"
DEBIAN_VERSION="${2:-bookworm}"
SIGNING_MARKER="$RELEASE_DIR/debian/.signed"

mkdir -p "$RELEASE_DIR"
rm -f "$SIGNING_MARKER"

# ── Normalize filenames ───────────────────────────────────────────────────────

find "$RELEASE_DIR" -maxdepth 1 -type f \( -name '*.deb' -o -name '*.ddeb' \) | while read -r f; do
    BASENAME=$(basename "$f")
    PKG_NAME="${BASENAME%%_*}"
    VERSION_RELEASE=$(printf '%s\n' "$BASENAME" | sed -E 's/^[^_]+_([^_]+)_.+$/\1/')
    PKG_EXT="${BASENAME##*.}"
    DPKG_ARCH=$(printf '%s\n' "$BASENAME" | sed -E 's/^[^_]+_[^_]+_([^.]+)\.(d?deb)$/\1/')
    DEST_DIR="$RELEASE_DIR/debian/${DEBIAN_VERSION}/${DPKG_ARCH}"
    mkdir -p "$DEST_DIR"
    cp "$f" "$DEST_DIR/${PKG_NAME}_${VERSION_RELEASE}_debian_${DPKG_ARCH}.${PKG_EXT}"
done

find "$RELEASE_DIR" -maxdepth 1 -type f \( -name '*.deb' -o -name '*.ddeb' \) -delete

# ── Generate per-arch Packages + Release ─────────────────────────────────────

for ARCH_DIR in $(find "$RELEASE_DIR/debian/${DEBIAN_VERSION}" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort); do
    DPKG_ARCH=$(basename "$ARCH_DIR")
    TMP_IDX=$(mktemp -d)
    find "$ARCH_DIR" -maxdepth 1 -type f -name "*_debian_${DPKG_ARCH}.deb" -exec cp {} "$TMP_IDX/" \;
    find "$ARCH_DIR" -maxdepth 1 -type f -name "*_debian_${DPKG_ARCH}.ddeb" -exec cp {} "$TMP_IDX/" \;
    (
        cd "$TMP_IDX"
        dpkg-scanpackages --multiversion . /dev/null > Packages
        if find . -maxdepth 1 -type f -name '*.ddeb' | grep -q .; then
            dpkg-scanpackages --type ddeb --multiversion . /dev/null >> Packages
        fi
        gzip -n9c Packages > Packages.gz
        apt-ftparchive release . > Release
        if [ -n "${DEBIAN_GPG_PRIVATE_KEY:-}" ]; then
            GNUPGHOME="$(mktemp -d)"
            export GNUPGHOME
            key_file="$(mktemp)"
            printf '%s\n' "$DEBIAN_GPG_PRIVATE_KEY" > "$key_file"
            gpg --batch --import "$key_file"
            key_fpr="$(gpg --batch --list-secret-keys --with-colons | awk -F: '$1 == "fpr" { print $10; exit }')"
            test -n "$key_fpr" || { echo "[collect-debian] Failed to determine imported GPG key fingerprint"; exit 1; }
            gpg --batch --yes --pinentry-mode loopback --default-key "$key_fpr" \
                --detach-sign -o Release.gpg Release
            gpg --batch --yes --pinentry-mode loopback --default-key "$key_fpr" \
                --clearsign -o InRelease Release
            rm -rf "$GNUPGHOME" "$key_file"
        fi
    )
    cp "$TMP_IDX/Packages"    "$ARCH_DIR/Packages"
    cp "$TMP_IDX/Packages.gz" "$ARCH_DIR/Packages.gz"
    cp "$TMP_IDX/Release"     "$ARCH_DIR/Release"
    if [ -f "$TMP_IDX/Release.gpg" ]; then
        cp "$TMP_IDX/Release.gpg" "$ARCH_DIR/Release.gpg"
    else
        rm -f "$ARCH_DIR/Release.gpg"
    fi
    if [ -f "$TMP_IDX/InRelease" ]; then
        cp "$TMP_IDX/InRelease" "$ARCH_DIR/InRelease"
        : > "$SIGNING_MARKER"
    else
        rm -f "$ARCH_DIR/InRelease"
    fi
    rm -rf "$TMP_IDX"
done
