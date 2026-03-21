#!/usr/bin/env bash

set -euo pipefail

WORK_ROOT="/work"
SDK_DIR="${WORK_ROOT}/sdk"
CONFIG_SRC="/opt/keen-pbr/packages.config"
CONFIG_CACHE="${SDK_DIR}/.keen-pbr-build.config"

job_config="$(python3 /opt/keen-pbr/generate_build_matrix.py "${OPENWRT_VERSION}" "${OPENWRT_TARGET}" "${OPENWRT_SUBTARGET}" | sed -n 's/^job-config=//p')"
[[ -n "${job_config}" ]] || { echo "Failed to resolve SDK metadata"; exit 1; }

sdk_file="$(python3 -c 'import json,sys; data=json.loads(sys.argv[1]); print(data[0]["sdk_file"])' "${job_config}")"
archive="${WORK_ROOT}/${sdk_file}"

mkdir -p "${WORK_ROOT}"
cd "${WORK_ROOT}"

if [[ ! -f "${archive}" ]]; then
  echo "[openwrt-image] Downloading ${sdk_file}..."
  wget -O "${archive}" "https://downloads.openwrt.org/releases/${OPENWRT_VERSION}/targets/${OPENWRT_TARGET}/${OPENWRT_SUBTARGET}/${sdk_file}"
fi

if [[ ! -d "${SDK_DIR}" ]]; then
  echo "[openwrt-image] Extracting SDK..."
  mkdir -p "${SDK_DIR}"
  tar --zstd -xf "${archive}" -C "${SDK_DIR}" --strip-components=1
fi

cd "${SDK_DIR}"
./scripts/feeds update -a
./scripts/feeds install -a

build_config="$(mktemp)"
target_sym="${OPENWRT_TARGET//-/_}"
subtarget_sym="${OPENWRT_SUBTARGET//-/_}"

cat >"${build_config}" <<EOF
CONFIG_TARGET_${target_sym}=y
CONFIG_TARGET_${target_sym}_${subtarget_sym}=y
# CONFIG_TARGET_MULTI_PROFILE is not set
# CONFIG_TARGET_ALL_PROFILES is not set
# CONFIG_ALL_KMODS is not set
# CONFIG_ALL_NONSHARED is not set
# CONFIG_AUTOREMOVE is not set
EOF
cat "${CONFIG_SRC}" >> "${build_config}"

cp "${build_config}" .config
cp "${build_config}" "${CONFIG_CACHE}"
rm -f "${build_config}"

make defconfig
touch .stamp-prepared
