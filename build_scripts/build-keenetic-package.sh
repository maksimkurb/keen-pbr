#!/bin/sh
# build-keenetic-package.sh — Compile keen-pbr inside an Entware build tree.
#
# Usage: scripts/build-keenetic-package.sh <workspace-dir> <entware-dir>
#
# This script is intended to run INSIDE the entware-builder Docker container
# (ghcr.io/maksimkurb/entware-builder:<config>), with the keen-pbr source tree
# mounted at <workspace-dir>.
#
# <workspace-dir>  Path to the keen-pbr source tree (contains version.mk, packages/, …)
# <entware-dir>    Path to the Entware build tree (e.g. /home/me/Entware)

set -eu

WORKSPACE="${1:?Usage: $0 <workspace-dir> <entware-dir>}"
ENTWARE_DIR="${2:?}"
FRONTEND_DIST="${KEEN_PBR_FRONTEND_DIST:-$WORKSPACE/frontend/dist}"

sh "$WORKSPACE/build_scripts/ensure-frontend-dist.sh" "$WORKSPACE" "$FRONTEND_DIST"

cd "$ENTWARE_DIR"
printf '\nsrc-link keenPbr %s/packages/keenetic\n' "$WORKSPACE" >> feeds.conf
./scripts/feeds update keenPbr
./scripts/feeds install -p keenPbr keen-pbr
FEED_PKG_DIR=$(find package -type d -path '*/keen-pbr' | grep '/package/feeds/' | head -1)
cp "$WORKSPACE/version.mk" "$FEED_PKG_DIR/version.mk"
cat "$WORKSPACE/packages/keenetic/packages.config" >> .config
make defconfig
make package/keen-pbr/compile V=s "-j$(nproc)" KEEN_PBR_SRC="$WORKSPACE" KEEN_PBR_FRONTEND_DIST="$FRONTEND_DIST"
