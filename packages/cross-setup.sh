#!/bin/bash
# cross-setup.sh — Download OpenWrt SDK and build cross-compile deps (no Docker)
#
# Usage: OPENWRT_SDK_URL=https://... packages/cross-setup.sh <toolchain-dir>
#
# After this runs, <toolchain-dir>/toolchain-aarch64_cortex-a53_gcc-13.3.0_musl
# and <toolchain-dir>/target-aarch64_cortex-a53_musl will be symlinked and ready
# for cmake/toolchain-aarch64-openwrt.cmake.

set -e -u

TOOLCHAIN_DIR="$(realpath "${1:?Usage: $0 <toolchain-dir>}")"
SDK_URL="${OPENWRT_SDK_URL:?Error: OPENWRT_SDK_URL is not set (e.g. https://downloads.openwrt.org/releases/24.10.4/targets/rockchip/armv8/openwrt-sdk-24.10.4-rockchip-armv8_gcc-13.3.0_musl.Linux-x86_64.tar.zst)}"

SDK_ARCHIVE="$TOOLCHAIN_DIR/$(basename "$SDK_URL")"
SDK_DIR="$TOOLCHAIN_DIR/sdk"

# ── 1. Download SDK ────────────────────────────────────────────────────────────
if [ ! -f "$SDK_ARCHIVE" ]; then
    echo "[cross-setup] Downloading OpenWrt SDK..."
    mkdir -p "$TOOLCHAIN_DIR"
    wget --show-progress -P "$TOOLCHAIN_DIR" "$SDK_URL"
fi

# ── 2. Extract SDK ─────────────────────────────────────────────────────────────
if [ ! -d "$SDK_DIR" ]; then
    echo "[cross-setup] Extracting SDK..."
    mkdir -p "$SDK_DIR"
    tar --zstd -xf "$SDK_ARCHIVE" -C "$SDK_DIR" --strip-components=1
fi

cd "$SDK_DIR"

# ── 3. Update feeds (once) ─────────────────────────────────────────────────────
if [ ! -f .stamp-feeds ]; then
    echo "[cross-setup] Updating feeds..."
    ./scripts/feeds clean
    ./scripts/feeds update -a
    ./scripts/feeds install -a
    touch .stamp-feeds
fi

# ── 4. Configure: enable only the deps keen-pbr3 needs ────────────────────────
echo "[cross-setup] Configuring deps..."
cat >.config <<'EOF'
CONFIG_PACKAGE_libcurl=y
CONFIG_PACKAGE_libmbedtls=y
CONFIG_PACKAGE_libnl-core=y
CONFIG_PACKAGE_libnl-route=y
CONFIG_PACKAGE_libstdcpp=y
CONFIG_PACKAGE_zlib=y
CONFIG_PACKAGE_libzstd=y
EOF
make defconfig

# ── 5. Build deps ──────────────────────────────────────────────────────────────
echo "[cross-setup] Building dependencies (this takes a while the first time)..."
NJOBS="$(nproc)"
make -j"$NJOBS" package/zlib/compile
make -j"$NJOBS" package/mbedtls/compile
make -j"$NJOBS" package/curl/compile
make -j"$NJOBS" package/libnl/compile
make -j"$NJOBS" package/gcc/compile 2>/dev/null || true  # libstdcpp comes from GCC runtime
make -j"$NJOBS" package/zstd/compile

# ── 6. Symlink toolchain + target sysroot into TOOLCHAIN_DIR ──────────────────
echo "[cross-setup] Linking toolchain and sysroot..."
STAGING="$SDK_DIR/staging_dir"

TC_DIR="$(ls -d "$STAGING"/toolchain-aarch64_cortex-a53* 2>/dev/null | head -1 || true)"
TGT_DIR="$(ls -d "$STAGING"/target-aarch64_cortex-a53* 2>/dev/null | head -1 || true)"

[ -n "$TC_DIR" ]  || { echo "Error: toolchain not found in $STAGING"; exit 1; }
[ -n "$TGT_DIR" ] || { echo "Error: target sysroot not found in $STAGING"; exit 1; }

ln -sfn "$TC_DIR"  "$TOOLCHAIN_DIR/$(basename "$TC_DIR")"
ln -sfn "$TGT_DIR" "$TOOLCHAIN_DIR/$(basename "$TGT_DIR")"

touch "$TOOLCHAIN_DIR/.stamp-extracted"
echo "[cross-setup] Done! Toolchain ready in $TOOLCHAIN_DIR"
