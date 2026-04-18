#!/usr/bin/env bash

set -euo pipefail

WORKSPACE="${1:?Usage: $0 <workspace-dir> <release-dir>}"
RELEASE_DIR="${2:?}"
FRONTEND_DIST="${KEEN_PBR_FRONTEND_DIST:-$WORKSPACE/frontend/dist}"
DEBIAN_VERSION="${DEBIAN_VERSION:-bookworm}"
VERSION_RELEASE="$(
    . "$WORKSPACE/version.mk"
    printf '%s-%s' "$KEEN_PBR_VERSION" "$KEEN_PBR_RELEASE"
)"

prepare_tree() {
    local variant="$1"
    local src_dir="$2"

    rm -rf "$src_dir"
    mkdir -p "$src_dir"
    rsync -a --delete \
        --exclude='.git' \
        --exclude='frontend/node_modules' \
        --exclude='frontend/dist' \
        --exclude='build' \
        "$WORKSPACE"/ "$src_dir"/
    rm -rf "$src_dir/debian"
    cp -a "$WORKSPACE/packages/debian/$variant/debian" "$src_dir/debian"
    sed -i "1s/(.*)/(${VERSION_RELEASE})/" "$src_dir/debian/changelog"
}

mkdir -p "$RELEASE_DIR"
bash "$WORKSPACE/build_scripts/ensure-frontend-dist.sh" "$WORKSPACE" "$FRONTEND_DIST"

BUILD_ROOT="$(mktemp -d /tmp/keen-pbr-debian.XXXXXX)"
FULL_SRC="$BUILD_ROOT/full"
HEADLESS_SRC="$BUILD_ROOT/headless"
trap 'rm -rf "$BUILD_ROOT"' EXIT

prepare_tree full "$FULL_SRC"
prepare_tree headless "$HEADLESS_SRC"

(
    cd "$FULL_SRC"
    KEEN_PBR_FRONTEND_DIST="$FRONTEND_DIST" dpkg-buildpackage -b -us -uc
)
find "$BUILD_ROOT" -maxdepth 1 -type f -name 'keen-pbr_*_*.deb' -exec cp -t "$RELEASE_DIR" {} +
find "$BUILD_ROOT" -maxdepth 1 -type f -name 'keen-pbr-dbgsym_*_*.ddeb' -exec cp -t "$RELEASE_DIR" {} +

(
    cd "$HEADLESS_SRC"
    dpkg-buildpackage -b -us -uc
)
find "$BUILD_ROOT" -maxdepth 1 -type f -name 'keen-pbr-headless_*_*.deb' -exec cp -t "$RELEASE_DIR" {} +
find "$BUILD_ROOT" -maxdepth 1 -type f -name 'keen-pbr-headless-dbgsym_*_*.ddeb' -exec cp -t "$RELEASE_DIR" {} +

bash "$WORKSPACE/build_scripts/collect-debian.sh" "$RELEASE_DIR" "$DEBIAN_VERSION"
