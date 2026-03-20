#!/usr/bin/env bash
#
# Build keen-pbr for a specified architecture using Meson.
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

CROSS_FILE="${PROJECT_DIR}/meson/cross/${ARCH}.ini"

# Validate architecture
if [ ! -f "$CROSS_FILE" ]; then
    echo "Error: Meson cross-file not found: ${CROSS_FILE}" >&2
    echo "Available architectures:" >&2
    ls -1 "${PROJECT_DIR}/meson/cross/" | sed 's/\.ini$//' >&2
    exit 1
fi

echo "=== Building keen-pbr for ${ARCH} ==="
echo "Project dir: ${PROJECT_DIR}"
echo "Build dir:   ${BUILD_DIR}"
echo "Cross-file:  ${CROSS_FILE}"
echo ""

# Step 1: Meson setup - configure the build (wraps auto-download deps)
echo "--- Meson setup ---"
meson setup "$BUILD_DIR" "$PROJECT_DIR" \
    --cross-file="$CROSS_FILE" \
    --wipe 2>/dev/null \
    || meson setup "$BUILD_DIR" "$PROJECT_DIR" \
        --cross-file="$CROSS_FILE"

# Step 2: Meson compile
echo "--- Meson compile ---"
meson compile -C "$BUILD_DIR"

# Step 3: Copy binary to dist directory
echo "--- Collecting output ---"
mkdir -p "$DIST_DIR"
if [ -f "$BUILD_DIR/keen-pbr" ]; then
    cp "$BUILD_DIR/keen-pbr" "$DIST_DIR/keen-pbr"
    echo "Binary: ${DIST_DIR}/keen-pbr"
    file "$DIST_DIR/keen-pbr"
else
    echo "Warning: Binary not found at ${BUILD_DIR}/keen-pbr"
    echo "Build directory contents:"
    ls -la "$BUILD_DIR/"
fi

echo ""
echo "=== Build complete for ${ARCH} ==="
