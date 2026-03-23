#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="${REPO_ROOT:-/src}"
RELEASE_DIR="${RELEASE_DIR:-/src/release_files}"
SDK_DIR="/work/sdk"
FRONTEND_OUT_DIR="/tmp/keen-pbr-frontend-dist"
job_count="$(( $(nproc) - 1 ))"

if (( job_count < 1 )); then
  job_count=1
fi

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

echo "[openwrt] Building frontend with bun..."
mkdir -p /tmp/keen-pbr-bun-tmp /tmp/keen-pbr-bun-cache
rm -rf "${FRONTEND_OUT_DIR}"

cd "${REPO_ROOT}/frontend"
TMPDIR=/tmp/keen-pbr-bun-tmp TEMP=/tmp/keen-pbr-bun-tmp TMP=/tmp/keen-pbr-bun-tmp BUN_INSTALL_CACHE_DIR=/tmp/keen-pbr-bun-cache bun install --frozen-lockfile
TMPDIR=/tmp/keen-pbr-bun-tmp TEMP=/tmp/keen-pbr-bun-tmp TMP=/tmp/keen-pbr-bun-tmp BUN_INSTALL_CACHE_DIR=/tmp/keen-pbr-bun-cache KEEN_PBR_FRONTEND_OUT_DIR="${FRONTEND_OUT_DIR}" bun run build
find "${FRONTEND_OUT_DIR}" -type f ! -name '*.gz' -delete

cd "${SDK_DIR}"
rm -rf package/keen-pbr
cp -r "${REPO_ROOT}/packages/openwrt/keen-pbr" package/

make package/keen-pbr/clean
make package/keen-pbr/compile V=s "-j${job_count}" KEEN_PBR_SRC="${REPO_ROOT}" KEEN_PBR_FRONTEND_DIST="${FRONTEND_OUT_DIR}"

pkg_version="$(sed -n 's/^PKG_VERSION:=//p' "${REPO_ROOT}/packages/openwrt/keen-pbr/Makefile" | head -1)"
find "${SDK_DIR}/bin" -type f \( -name 'keen-pbr*.ipk' -o -name 'keen-pbr*.apk' \) | while read -r file; do
  ext="${file##*.}"
  cp "$file" "${RELEASE_DIR}/keen-pbr_${pkg_version}_openwrt_${OPENWRT_VERSION}_${OPENWRT_TARGET}_${OPENWRT_SUBTARGET}.${ext}"
done
