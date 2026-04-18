#!/bin/sh
# collect-keenetic.sh — Rename compiled Keenetic packages and generate repository index.
#
# Usage: scripts/collect-keenetic.sh \
#          <workspace-dir> <entware-bin-dir> <release-dir> <config-name> [keenetic-version]
#
# This script is intended to run INSIDE the entware-builder Docker container
# after build-keenetic-package.sh has completed.
#
# <workspace-dir>    Root of the keen-pbr source tree (contains version.mk, scripts/)
# <entware-bin-dir>  Entware bin output directory (e.g. /home/me/Entware/bin)
# <release-dir>      Directory where repository-layout artifacts are written
# <config-name>      Entware config name, e.g. "mipsel-3.4"
# [keenetic-version] Keenetic channel version directory, default: "current"

set -eu

WORKSPACE="${1:?Usage: $0 <workspace-dir> <entware-bin-dir> <release-dir> <config-name>}"
ENTWARE_BIN_DIR="${2:?}"
RELEASE_DIR="${3:?}"
CONFIG_NAME="${4:?}"
KEENETIC_VERSION="${5:-current}"
ENTWARE_ROOT="$(dirname "$ENTWARE_BIN_DIR")"

. "$WORKSPACE/version.mk"
VERSION_RELEASE="${KEEN_PBR_VERSION}-${KEEN_PBR_RELEASE}"
PKG_ARCH=$(printf '%s' "$CONFIG_NAME" | cut -d'-' -f1)
DEST_DIR="$RELEASE_DIR/keenetic/${KEENETIC_VERSION}/${PKG_ARCH}"
DEBUG_DEST_DIR="$RELEASE_DIR/keenetic-debug/${KEENETIC_VERSION}/${PKG_ARCH}"

mkdir -p "$DEST_DIR"
mkdir -p "$DEBUG_DEST_DIR"

# ── Copy and rename packages ──────────────────────────────────────────────────

find "$ENTWARE_BIN_DIR" -type f -path '*/packages/*.ipk' -name 'keen-pbr_*.ipk' | while read -r f; do
    cp "$f" "$DEST_DIR/keen-pbr_${VERSION_RELEASE}_keenetic_${CONFIG_NAME}.ipk"
done
find "$ENTWARE_BIN_DIR" -type f -path '*/packages/*.ipk' -name 'keen-pbr-headless_*.ipk' | while read -r f; do
    cp "$f" "$DEST_DIR/keen-pbr-headless_${VERSION_RELEASE}_keenetic_${CONFIG_NAME}.ipk"
done

find "$ENTWARE_ROOT" -type f -path '*/debug-artifacts/*/keen-pbr.debug' | while read -r f; do
    variant="$(basename "$(dirname "$f")")"
    cp "$f" "$DEBUG_DEST_DIR/keen-pbr_${VERSION_RELEASE}_keenetic_${CONFIG_NAME}_${variant}.debug"
done

# ── Generate IPK Packages index ───────────────────────────────────────────────

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
cp "$DEST_DIR/"*_keenetic_"${CONFIG_NAME}".ipk "$TMP_DIR/"
(
    cd "$TMP_DIR"
    python3 "$WORKSPACE/build_scripts/ipk-make-index.py" .
)
cp "$TMP_DIR/Packages"    "$DEST_DIR/Packages"
cp "$TMP_DIR/Packages.gz" "$DEST_DIR/Packages.gz"
