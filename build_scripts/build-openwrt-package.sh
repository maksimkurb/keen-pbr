#!/bin/bash
# build-openwrt-package.sh — Compile keen-pbr inside an extracted OpenWrt SDK.
#
# Usage: scripts/build-openwrt-package.sh <workspace-dir> <sdk-dir>
#
# <workspace-dir>  Root of the keen-pbr source tree (contains version.mk, packages/, …)
# <sdk-dir>        Extracted OpenWrt SDK directory (contains scripts/feeds, .config, …)
#
set -euo pipefail

WORKSPACE="${1:?Usage: $0 <workspace-dir> <sdk-dir>}"
SDK_DIR="${2:?}"
FRONTEND_DIST="${KEEN_PBR_FRONTEND_DIST:-$WORKSPACE/frontend/dist}"
KEEN_PBR_RELEASE="$(bash "$WORKSPACE/build_scripts/resolve-version.sh" release "$WORKSPACE")"
FEED_UPDATE_RETRIES="${FEED_UPDATE_RETRIES:-3}"
FEED_UPDATE_RETRY_DELAY="${FEED_UPDATE_RETRY_DELAY:-5}"

retry() {
    local attempts="${1:?attempt count required}"
    local delay="${2:?delay required}"
    shift 2
    local try=1
    while true; do
        if "$@"; then
            return 0
        fi
        if [ "$try" -ge "$attempts" ]; then
            return 1
        fi
        echo "[build-openwrt-package] Command failed on attempt $try/$attempts: $*" >&2
        sleep "$delay"
        try=$((try + 1))
    done
}

verify_feed_update() {
    local required_feeds="base packages luci routing telephony"
    local feed
    for feed in $required_feeds; do
        if [ ! -d "$SDK_DIR/feeds/$feed" ]; then
            echo "[build-openwrt-package] Missing feed after update: feeds/$feed" >&2
            return 1
        fi
    done
}

update_feeds() {
    rm -rf "$SDK_DIR/feeds/base" \
           "$SDK_DIR/feeds/packages" \
           "$SDK_DIR/feeds/luci" \
           "$SDK_DIR/feeds/routing" \
           "$SDK_DIR/feeds/telephony"
    ./scripts/feeds update -a
    verify_feed_update
}

install_required_feed_packages() {
    local packages="
        dnsmasq-full
        libatomic
        libcurl
        libnl-core
        libnl-route
        libstdcpp
        zlib
        libzstd
    "
    local pkg
    for pkg in $packages; do
        ./scripts/feeds install "$pkg"
    done
}

bash "$WORKSPACE/build_scripts/ensure-frontend-dist.sh" "$WORKSPACE" "$FRONTEND_DIST"

cp -r "$WORKSPACE/packages/openwrt/keen-pbr" "$SDK_DIR/package/"
cp "$WORKSPACE/version.mk" "$SDK_DIR/package/keen-pbr/version.mk"

cd "$SDK_DIR"
retry "$FEED_UPDATE_RETRIES" "$FEED_UPDATE_RETRY_DELAY" update_feeds || {
    echo "[build-openwrt-package] Feed update failed after $FEED_UPDATE_RETRIES attempts." >&2
    exit 1
}
install_required_feed_packages

cp "$WORKSPACE/packages/openwrt/packages.config" .config
make defconfig
make package/keen-pbr/compile V=s "-j$(nproc)" \
    KEEN_PBR_SRC="$WORKSPACE" \
    KEEN_PBR_FRONTEND_DIST="$FRONTEND_DIST" \
    KEEN_PBR_RELEASE="$KEEN_PBR_RELEASE"
