#!/usr/bin/env bash

set -euo pipefail

SDK_DIR="${1:?Usage: $0 <sdk-dir> <release-dir> <openwrt-version>}"
RELEASE_DIR="${2:?}"
TAG="${3:?}"

OPENWRT_ROOT="$RELEASE_DIR/openwrt/$TAG"
USIGN_BIN="$SDK_DIR/staging_dir/host/bin/usign"
APK_BIN="$SDK_DIR/staging_dir/host/bin/apk"
APK_SIGNING_MARKER="$RELEASE_DIR/openwrt/.apk-signed"

if [ ! -d "$OPENWRT_ROOT" ]; then
    echo "[sign-openwrt-repository] OpenWrt repository tree not found at $OPENWRT_ROOT" >&2
    exit 1
fi

has_packages_files=false
has_apk_files=false

find "$OPENWRT_ROOT" -mindepth 1 -maxdepth 2 -type f -name 'Packages' | grep -q . && has_packages_files=true || true
find "$OPENWRT_ROOT" -mindepth 1 -maxdepth 2 -type f -name '*.apk' | grep -q . && has_apk_files=true || true

if $has_packages_files; then
    if [ -z "${OPENWRT_USIGN_PRIVATE_KEY:-}" ]; then
        echo "[sign-openwrt-repository] OPENWRT_USIGN_PRIVATE_KEY is required" >&2
        exit 1
    fi
    if [ ! -x "$USIGN_BIN" ]; then
        echo "[sign-openwrt-repository] usign not found at $USIGN_BIN" >&2
        exit 1
    fi
fi

if $has_apk_files; then
    if [ -z "${OPENWRT_APK_PRIVATE_KEY:-}" ]; then
        echo "[sign-openwrt-repository] OPENWRT_APK_PRIVATE_KEY is required" >&2
        exit 1
    fi
    if [ ! -x "$APK_BIN" ]; then
        echo "[sign-openwrt-repository] apk not found at $APK_BIN" >&2
        exit 1
    fi
fi

rm -f "$APK_SIGNING_MARKER"

find "$OPENWRT_ROOT" -mindepth 1 -maxdepth 1 -type d | sort | while read -r arch_dir; do
    packages_file="$arch_dir/Packages"
    if [ -f "$packages_file" ]; then
        key_file="$(mktemp)"
        printf '%s\n' "$OPENWRT_USIGN_PRIVATE_KEY" > "$key_file"
        "$USIGN_BIN" -S -m "$packages_file" -s "$key_file" -x "$arch_dir/Packages.sig"
        rm -f "$key_file"
    fi

    mapfile -t arch_apks < <(find "$arch_dir" -maxdepth 1 -type f -name '*.apk' -printf '%f\n' | sort)
    if [ "${#arch_apks[@]}" -gt 0 ]; then
        tmp_dir="$(mktemp -d)"
        key_file="$(mktemp)"
        for apk_file in "${arch_apks[@]}"; do
            cp "$arch_dir/$apk_file" "$tmp_dir/"
        done
        printf '%s\n' "$OPENWRT_APK_PRIVATE_KEY" > "$key_file"
        (
            cd "$tmp_dir"
            rm -f packages.adb
            "$APK_BIN" mkndx --allow-untrusted \
                --sign "$key_file" \
                --output "packages.adb" \
                "${arch_apks[@]}"
        )
        cp "$tmp_dir/packages.adb" "$arch_dir/packages.adb"
        rm -rf "$tmp_dir"
        rm -f "$key_file"
        : > "$APK_SIGNING_MARKER"
    fi
done
