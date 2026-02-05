#!/usr/bin/env bash
#
# Build keen-pbr3 for a specified architecture using Conan + Meson.
#
# Usage: ./docker/build.sh <architecture>
#
# Supported architectures:
#   mips-be-openwrt    - MIPS big-endian (OpenWRT)
#   mips-le-openwrt    - MIPS little-endian (OpenWRT)
#   arm-openwrt        - ARM (OpenWRT)
#   aarch64-openwrt    - AArch64 (OpenWRT)
#   x86_64-openwrt     - x86_64 (OpenWRT)
#   mips-le-keenetic   - MIPS little-endian (Keenetic)
#
set -euo pipefail

ARCH="${1:?Usage: $0 <architecture>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build/${ARCH}"
DIST_DIR="${PROJECT_DIR}/dist/${ARCH}"

CONAN_PROFILE="${PROJECT_DIR}/conan/profiles/${ARCH}"
CROSS_FILE="${PROJECT_DIR}/meson/cross/${ARCH}.ini"

# Validate architecture
if [ ! -f "$CONAN_PROFILE" ]; then
    echo "Error: Conan profile not found: ${CONAN_PROFILE}" >&2
    echo "Available architectures:" >&2
    ls -1 "${PROJECT_DIR}/conan/profiles/" | grep -v base- >&2
    exit 1
fi

if [ ! -f "$CROSS_FILE" ]; then
    echo "Error: Meson cross-file not found: ${CROSS_FILE}" >&2
    exit 1
fi

echo "=== Building keen-pbr3 for ${ARCH} ==="
echo "Project dir:  ${PROJECT_DIR}"
echo "Build dir:    ${BUILD_DIR}"
echo "Conan profile: ${CONAN_PROFILE}"
echo "Cross-file:   ${CROSS_FILE}"
echo ""

# Step 1: Conan install - resolve dependencies and generate toolchain files
echo "--- Conan install ---"
conan install "$PROJECT_DIR" \
    --profile:host="$CONAN_PROFILE" \
    --profile:build=default \
    --output-folder="$BUILD_DIR" \
    --build=missing

# Step 2: Meson setup - configure the build
echo "--- Meson setup ---"
meson setup "$BUILD_DIR" "$PROJECT_DIR" \
    --cross-file="$CROSS_FILE" \
    --native-file="$BUILD_DIR/conan_meson_native.ini" \
    --wipe 2>/dev/null \
    || meson setup "$BUILD_DIR" "$PROJECT_DIR" \
        --cross-file="$CROSS_FILE" \
        --native-file="$BUILD_DIR/conan_meson_native.ini"

# Step 3: Meson compile
echo "--- Meson compile ---"
meson compile -C "$BUILD_DIR"

# Step 4: Copy binary to dist directory
echo "--- Collecting output ---"
mkdir -p "$DIST_DIR"
if [ -f "$BUILD_DIR/keen-pbr3" ]; then
    cp "$BUILD_DIR/keen-pbr3" "$DIST_DIR/keen-pbr3"
    echo "Binary: ${DIST_DIR}/keen-pbr3"
    file "$DIST_DIR/keen-pbr3"
else
    echo "Warning: Binary not found at ${BUILD_DIR}/keen-pbr3"
    echo "Build directory contents:"
    ls -la "$BUILD_DIR/"
fi

echo ""
echo "=== Build complete for ${ARCH} ==="
