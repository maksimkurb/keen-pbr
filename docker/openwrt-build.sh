#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="${REPO_ROOT:-/src}"
RELEASE_DIR="${RELEASE_DIR:-/src/release_files}"

mkdir -p "$RELEASE_DIR"
cd /work

job_config="$(python3 "${REPO_ROOT}/.github/builder/generate_build_matrix.py" "${OPENWRT_VERSION}" "${OPENWRT_TARGET}" "${OPENWRT_SUBTARGET}" | sed -n 's/^job-config=//p')"
[[ -n "$job_config" ]] || { echo "Failed to resolve SDK metadata"; exit 1; }

sdk_file="$(python3 -c 'import json,sys; data=json.loads(sys.argv[1]); print(data[0]["sdk_file"])' "$job_config")"
archive="/work/${sdk_file}"
sdk_dir="/work/sdk"

if [[ ! -f "$archive" ]]; then
  echo "[openwrt] Downloading ${sdk_file}..."
  wget -O "$archive" "https://downloads.openwrt.org/releases/${OPENWRT_VERSION}/targets/${OPENWRT_TARGET}/${OPENWRT_SUBTARGET}/${sdk_file}"
fi

if [[ ! -d "$sdk_dir" ]]; then
  echo "[openwrt] Extracting SDK..."
  mkdir -p "$sdk_dir"
  tar --zstd -xf "$archive" -C "$sdk_dir" --strip-components=1
fi

cd "$sdk_dir"

if [[ ! -f .stamp-feeds ]]; then
  ./scripts/feeds update -a
  ./scripts/feeds install -a
  touch .stamp-feeds
fi

rm -rf package/keen-pbr
cp -r "${REPO_ROOT}/packages/openwrt/keen-pbr" package/
cp "${REPO_ROOT}/packages/openwrt/packages.config" .config

make defconfig
make package/keen-pbr/clean
make package/keen-pbr/compile V=s "-j$(nproc)" KEEN_PBR3_SRC="${REPO_ROOT}"

pkg_version="$(sed -n 's/^PKG_VERSION:=//p' "${REPO_ROOT}/packages/openwrt/keen-pbr/Makefile" | head -1)"
find "${sdk_dir}/bin" -type f \( -name 'keen-pbr*.ipk' -o -name 'keen-pbr*.apk' \) | while read -r file; do
  ext="${file##*.}"
  cp "$file" "${RELEASE_DIR}/keen-pbr_${pkg_version}_openwrt_${OPENWRT_TARGET}_${OPENWRT_SUBTARGET}.${ext}"
done
