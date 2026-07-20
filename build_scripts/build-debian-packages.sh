#!/usr/bin/env bash

set -euo pipefail

WORKSPACE="${1:?Usage: $0 <workspace-dir> <release-dir>}"
RELEASE_DIR="${2:?}"
FRONTEND_DIST="${KEEN_PBR_FRONTEND_DIST:-$WORKSPACE/frontend/dist}"
DEBIAN_VERSION="${DEBIAN_VERSION:-bookworm}"
DEBIAN_VARIANTS="${DEBIAN_VARIANTS:-full headless}"
KEEN_PBR_VERSION="$(bash "$WORKSPACE/build_scripts/resolve-version.sh" version "$WORKSPACE")"
KEEN_PBR_RELEASE="$(bash "$WORKSPACE/build_scripts/resolve-version.sh" release "$WORKSPACE")"
VERSION_RELEASE="${KEEN_PBR_VERSION}-${KEEN_PBR_RELEASE}"

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
trap 'rm -rf "$BUILD_ROOT"' EXIT

for variant in $DEBIAN_VARIANTS; do
    case "$variant" in
        full|headless) ;;
        *) echo "Unsupported DEBIAN_VARIANTS entry: $variant" >&2; exit 2 ;;
    esac

    src_dir="$BUILD_ROOT/$variant"
    prepare_tree "$variant" "$src_dir"
    (
        cd "$src_dir"
        if [ "$variant" = "full" ]; then
            KEEN_PBR_FRONTEND_DIST="$FRONTEND_DIST" \
            KEEN_PBR_RELEASE_OVERRIDE="$KEEN_PBR_RELEASE" \
            dpkg-buildpackage -b -us -uc
        else
            KEEN_PBR_RELEASE_OVERRIDE="$KEEN_PBR_RELEASE" dpkg-buildpackage -b -us -uc
        fi
    )
done

find "$BUILD_ROOT" -maxdepth 1 -type f \( -name 'keen-pbr_*_*.deb' -o -name 'keen-pbr-headless_*_*.deb' \
    -o -name 'keen-pbr-dbgsym_*_*.ddeb' -o -name 'keen-pbr-headless-dbgsym_*_*.ddeb' \) \
    -exec cp -t "$RELEASE_DIR" {} +

bash "$WORKSPACE/build_scripts/collect-debian.sh" "$RELEASE_DIR" "$DEBIAN_VERSION"
