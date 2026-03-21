#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="${REPO_ROOT:-/src}"
RELEASE_DIR="${RELEASE_DIR:-/src/release_files}"
SDK_DIR="/work/sdk"

mkdir -p "$RELEASE_DIR"
cd "${SDK_DIR}"

if [[ ! -d "${SDK_DIR}" ]] || [[ ! -f .stamp-prepared ]]; then
  echo "Prepared OpenWrt SDK not found in image. Rebuild the OpenWrt builder image." >&2
  exit 1
fi

if [[ -f /opt/keen-pbr/packages.config ]] && ! cmp -s "${REPO_ROOT}/packages/openwrt/packages.config" /opt/keen-pbr/packages.config; then
  echo "[openwrt] Warning: packages/openwrt/packages.config differs from the config baked into the image."
  echo "[openwrt] Rebuild the OpenWrt builder image to apply dependency/config changes."
fi

rm -rf package/keen-pbr
cp -r "${REPO_ROOT}/packages/openwrt/keen-pbr" package/

make package/keen-pbr/clean
make package/keen-pbr/compile V=s "-j$(nproc)" KEEN_PBR_SRC="${REPO_ROOT}"

pkg_version="$(sed -n 's/^PKG_VERSION:=//p' "${REPO_ROOT}/packages/openwrt/keen-pbr/Makefile" | head -1)"
find "${SDK_DIR}/bin" -type f \( -name 'keen-pbr*.ipk' -o -name 'keen-pbr*.apk' \) | while read -r file; do
  ext="${file##*.}"
  cp "$file" "${RELEASE_DIR}/keen-pbr_${pkg_version}_openwrt_${OPENWRT_VERSION}_${OPENWRT_TARGET}_${OPENWRT_SUBTARGET}.${ext}"
done
