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

bash "$WORKSPACE/build_scripts/ensure-frontend-dist.sh" "$WORKSPACE" "$FRONTEND_DIST"

cp -r "$WORKSPACE/packages/openwrt/keen-pbr" "$SDK_DIR/package/"
cp "$WORKSPACE/version.mk" "$SDK_DIR/package/keen-pbr/version.mk"

cd "$SDK_DIR"
./scripts/feeds update -a
./scripts/feeds install -a

cp "$WORKSPACE/packages/openwrt/packages.config" .config
make defconfig
make package/keen-pbr/compile V=s "-j$(nproc)" \
    KEEN_PBR_SRC="$WORKSPACE" \
    KEEN_PBR_FRONTEND_DIST="$FRONTEND_DIST" \
    KEEN_PBR_RELEASE="$KEEN_PBR_RELEASE"
