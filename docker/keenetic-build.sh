#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="${REPO_ROOT:-/src}"
RELEASE_DIR="${RELEASE_DIR:-/src/release_files}"
FRONTEND_OUT_DIR="/tmp/keen-pbr-frontend-dist"
BUILD_HEADLESS="${BUILD_HEADLESS:-false}"
job_count="$(( $(nproc) - 1 ))"

if (( job_count < 1 )); then
  job_count=1
fi

mkdir -p "$RELEASE_DIR"
. "${REPO_ROOT}/version.mk"
version_release="${KEEN_PBR_VERSION}-${KEEN_PBR_RELEASE}"

echo "[keenetic] Building frontend with bun..."
mkdir -p /tmp/keen-pbr-bun-tmp /tmp/keen-pbr-bun-cache
rm -rf "${FRONTEND_OUT_DIR}"

cd "${REPO_ROOT}/frontend"
TMPDIR=/tmp/keen-pbr-bun-tmp TEMP=/tmp/keen-pbr-bun-tmp TMP=/tmp/keen-pbr-bun-tmp BUN_INSTALL_CACHE_DIR=/tmp/keen-pbr-bun-cache bun install --frozen-lockfile
TMPDIR=/tmp/keen-pbr-bun-tmp TEMP=/tmp/keen-pbr-bun-tmp TMP=/tmp/keen-pbr-bun-tmp BUN_INSTALL_CACHE_DIR=/tmp/keen-pbr-bun-cache KEEN_PBR_FRONTEND_OUT_DIR="${FRONTEND_OUT_DIR}" bun run build
find "${FRONTEND_OUT_DIR}" -type f ! -name '*.gz' -delete

cd /home/me/Entware

grep -Fqx "src-link keenPbr ${REPO_ROOT}/packages/keenetic" feeds.conf || \
  printf '\nsrc-link keenPbr %s/packages/keenetic\n' "$REPO_ROOT" >> feeds.conf

./scripts/feeds update keenPbr
./scripts/feeds install -p keenPbr keen-pbr
feed_pkg_dir="$(find /home/me/Entware/package -type d -path '*/keen-pbr' | grep '/package/feeds/' | head -1)"
cp "${REPO_ROOT}/version.mk" "${feed_pkg_dir}/version.mk"

touch .config
while IFS= read -r line || [[ -n "$line" ]]; do
  if [[ -z "$line" ]]; then
    printf '\n' >> .config
  elif ! grep -Fqx "$line" .config; then
    printf '%s\n' "$line" >> .config
  fi
done < "${REPO_ROOT}/packages/keenetic/packages.config"

make defconfig
make package/keen-pbr/clean
make package/keen-pbr/compile V=s "-j${job_count}" KEEN_PBR_SRC="${REPO_ROOT}" KEEN_PBR_FRONTEND_DIST="${FRONTEND_OUT_DIR}"

find /home/me/Entware/bin -type f -path '*/packages/*.ipk' -name 'keen-pbr_*.ipk' | while read -r file; do
  cp "$file" "${RELEASE_DIR}/keen-pbr_${version_release}_keenetic_${KEENETIC_ARCH}.ipk"
done

if [[ "${BUILD_HEADLESS,,}" == "true" ]]; then
  find /home/me/Entware/bin -type f -path '*/packages/*.ipk' -name 'keen-pbr-headless_*.ipk' | while read -r file; do
    cp "$file" "${RELEASE_DIR}/keen-pbr-headless_${version_release}_keenetic_${KEENETIC_ARCH}.ipk"
  done
fi
