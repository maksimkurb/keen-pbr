#!/bin/bash
# collect-openwrt.sh — Rename compiled OpenWrt packages and generate repository indexes.
#
# Usage: scripts/collect-openwrt.sh \
#          <workspace-dir> <sdk-dir> <release-dir> \
#          <tag> <architecture> [pkgarch]
#
# <workspace-dir>  Root of the keen-pbr source tree (contains version.mk)
# <sdk-dir>        Extracted OpenWrt SDK directory (built packages are in sdk-dir/bin/)
# <release-dir>    Directory where repository-layout artifacts are written (created if absent)
# <tag>            OpenWrt version string, e.g. "24.10.4"
# <architecture>   OpenWrt package architecture, e.g. "aarch64_cortex-a53"
# [pkgarch]        Optional explicit package architecture; auto-detected from path if empty

set -euo pipefail

WORKSPACE="${1:?Usage: $0 <workspace-dir> <sdk-dir> <release-dir> <tag> <architecture> [pkgarch]}"
SDK_DIR="${2:?}"
RELEASE_DIR="${3:?}"
TAG="${4:?}"
ARCHITECTURE="${5:?}"
FIXED_PKGARCH="${6:-}"
APK_SIGNING_MARKER="$RELEASE_DIR/openwrt/.apk-signed"

VERSION_RELEASE="$(bash "$WORKSPACE/build_scripts/resolve-version.sh" full "$WORKSPACE")"
DEBUG_DEST_ROOT="$RELEASE_DIR/openwrt-debug/${TAG}"

mkdir -p "$RELEASE_DIR"
mkdir -p "$DEBUG_DEST_ROOT"

# ── Copy and rename packages ──────────────────────────────────────────────────

_copy_pkg() {
    local name_prefix="$1"  # "keen-pbr" or "keen-pbr-headless"
    find "$SDK_DIR/bin" -type f \( -name "${name_prefix}_*.ipk" -o -name "${name_prefix}-*.apk" \) | while read -r f; do
        EXT="${f##*.}"
        PKG_ARCH="$FIXED_PKGARCH"
        if [ -z "$PKG_ARCH" ]; then
            PKG_ARCH=$(printf '%s\n' "$f" | sed -E 's#^.*/bin/packages/([^/]+)/.*$#\1#')
        fi
        if [ -z "$PKG_ARCH" ] || [ "$PKG_ARCH" = "$f" ]; then
            BASENAME=$(basename "$f")
            PKG_ARCH=$(printf '%s\n' "$BASENAME" | sed -E 's/^[^_]+_[^_]+_(.+)\.[^.]+$/\1/')
        fi
        [ -n "$PKG_ARCH" ] || PKG_ARCH="$ARCHITECTURE"
        ARCH_DIR_NAME="${PKG_ARCH}"
        DEST_DIR="$RELEASE_DIR/openwrt/${TAG}/${ARCH_DIR_NAME}"
        mkdir -p "$DEST_DIR"
        cp "$f" "$DEST_DIR/${name_prefix}_${VERSION_RELEASE}_openwrt_${TAG}_${ARCHITECTURE}.${EXT}"
    done
}

_copy_pkg "keen-pbr"
_copy_pkg "keen-pbr-headless"

find "$SDK_DIR/build_dir" -type f -path '*/debug-artifacts/*/keen-pbr.debug' | while read -r f; do
    variant="$(basename "$(dirname "$f")")"
    cp "$f" "$DEBUG_DEST_ROOT/keen-pbr_${VERSION_RELEASE}_openwrt_${TAG}_${ARCHITECTURE}_${variant}.debug"
done

# ── IPK: generate Packages index per architecture ────────────────────────────

IPKG_INDEXER="$SDK_DIR/scripts/ipkg-make-index.sh"
USIGN_BIN="$SDK_DIR/staging_dir/host/bin/usign"
if [ -x "$IPKG_INDEXER" ]; then
    # Provide sha256 shim if only sha256sum is available (Linux default)
    if ! command -v sha256 >/dev/null 2>&1 && command -v sha256sum >/dev/null 2>&1; then
        TMP_BIN=$(mktemp -d)
        trap 'rm -rf "$TMP_BIN"' EXIT
        cat > "$TMP_BIN/sha256" <<'EOF'
#!/usr/bin/env bash
sha256sum "$1" | awk '{print $1}'
EOF
        chmod +x "$TMP_BIN/sha256"
        export PATH="$TMP_BIN:$PATH"
    fi

    for ARCH_DIR in $(find "$RELEASE_DIR/openwrt/${TAG}" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort); do
        ARCH_DIR_NAME=$(basename "$ARCH_DIR")
        PKG_ARCH="${ARCH_DIR_NAME}"
        mapfile -t ARCH_IPKS < <(find "$ARCH_DIR" -maxdepth 1 -type f \
            -name "keen-pbr*_openwrt_${TAG}_${ARCHITECTURE}.ipk" -printf '%f\n' | sort)
        if [ "${#ARCH_IPKS[@]}" -gt 0 ]; then
            TMP_DIR=$(mktemp -d)
            for f in "${ARCH_IPKS[@]}"; do
                cp "$ARCH_DIR/$f" "$TMP_DIR/"
            done
            (
                cd "$TMP_DIR"
                "$IPKG_INDEXER" . > Packages.manifest
                grep -vE '^(Maintainer|LicenseFiles|Source|SourceName|Require|SourceDateEpoch)' \
                    Packages.manifest > Packages
                gzip -n9c Packages > Packages.gz
                if [ -n "${OPENWRT_USIGN_PRIVATE_KEY:-}" ]; then
                    if [ ! -x "$USIGN_BIN" ]; then
                        echo "[collect-openwrt] No usign found at $USIGN_BIN; skipping Packages signature."
                    else
                        key_file="$(mktemp)"
                        printf '%s\n' "$OPENWRT_USIGN_PRIVATE_KEY" > "$key_file"
                        "$USIGN_BIN" -S -m Packages -s "$key_file" -x Packages.sig
                        rm -f "$key_file"
                    fi
                fi
            )
            cp "$TMP_DIR/Packages"    "$ARCH_DIR/Packages"
            cp "$TMP_DIR/Packages.manifest" "$ARCH_DIR/Packages.manifest"
            cp "$TMP_DIR/Packages.gz" "$ARCH_DIR/Packages.gz"
            if [ -f "$TMP_DIR/Packages.sig" ]; then
                cp "$TMP_DIR/Packages.sig" "$ARCH_DIR/Packages.sig"
            else
                rm -f "$ARCH_DIR/Packages.sig"
            fi
            rm -rf "$TMP_DIR"
        fi
    done
else
    echo "[collect-openwrt] No ipkg indexer found at $IPKG_INDEXER; skipping IPK index generation."
fi

# ── APK: generate .adb index per architecture ────────────────────────────────

APK_BIN="$SDK_DIR/staging_dir/host/bin/apk"
if [ ! -x "$APK_BIN" ]; then
    echo "[collect-openwrt] No OpenWrt host apk tool found at $APK_BIN; skipping .adb generation."
    APK_BIN=""
fi

if [ -n "$APK_BIN" ] && find "$RELEASE_DIR/openwrt/${TAG}" -type f \
        -name "keen-pbr*_openwrt_${TAG}_${ARCHITECTURE}.apk" 2>/dev/null | grep -q .; then
    rm -f "$APK_SIGNING_MARKER"
    for ARCH_DIR in $(find "$RELEASE_DIR/openwrt/${TAG}" -mindepth 1 -maxdepth 1 -type d | sort); do
        ARCH_DIR_NAME=$(basename "$ARCH_DIR")
        PKG_ARCH="${ARCH_DIR_NAME}"
        mapfile -t ARCH_APKS < <(find "$ARCH_DIR" -maxdepth 1 -type f \
            -name "keen-pbr*_openwrt_${TAG}_${ARCHITECTURE}.apk" -printf '%f\n' | sort)
        if [ "${#ARCH_APKS[@]}" -gt 0 ]; then
            (
                cd "$ARCH_DIR"
                if [ -n "${OPENWRT_APK_PRIVATE_KEY:-}" ]; then
                    key_file="$(mktemp)"
                    printf '%s\n' "$OPENWRT_APK_PRIVATE_KEY" > "$key_file"
                    "$APK_BIN" mkndx --allow-untrusted \
                        --sign "$key_file" \
                        --output "packages.adb" \
                        "${ARCH_APKS[@]}"
                    rm -f "$key_file"
                    : > "$APK_SIGNING_MARKER"
                else
                    "$APK_BIN" mkndx --allow-untrusted \
                        --output "packages.adb" \
                        "${ARCH_APKS[@]}"
                fi
            )
        fi
    done
else
    echo "[collect-openwrt] No .apk artifacts found; skipping .adb generation."
    rm -f "$APK_SIGNING_MARKER"
fi
