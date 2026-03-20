#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="${REPO_ROOT:-/src}"
RELEASE_DIR="${RELEASE_DIR:-/src/release_files}"

mkdir -p "$RELEASE_DIR"
cd /home/me/Entware

grep -Fqx "src-link keenPbr ${REPO_ROOT}/packages/keenetic" feeds.conf || \
  printf '\nsrc-link keenPbr %s/packages/keenetic\n' "$REPO_ROOT" >> feeds.conf

./scripts/feeds update keenPbr
./scripts/feeds install -p keenPbr keen-pbr

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
make package/keen-pbr/compile V=s "-j$(nproc)" KEEN_PBR3_SRC="${REPO_ROOT}"

pkg_version="$(sed -n 's/^PKG_VERSION:=//p' "${REPO_ROOT}/packages/keenetic/keen-pbr/Makefile" | head -1)"
find /home/me/Entware/bin -type f -path '*/packages/*.ipk' -name 'keen-pbr*.ipk' | while read -r file; do
  cp "$file" "${RELEASE_DIR}/keen-pbr_${pkg_version}_keenetic_${KEENETIC_ARCH}.ipk"
done
