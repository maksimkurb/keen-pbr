#!/bin/bash
# openwrt-sdk-setup.sh — Download and extract the OpenWrt SDK for a given release.
#
# Usage: scripts/openwrt-sdk-setup.sh <version> <target> <subtarget> <sdk-stamp-dir>
#
# The SDK is extracted into <sdk-stamp-dir>/openwrt-sdk-<...>/
# A stamp file <sdk-stamp-dir>/.stamp-sdk-ready is created; subsequent calls are no-ops.

set -euo pipefail

VERSION="${1:?Usage: $0 <version> <target> <subtarget> <sdk-stamp-dir>}"
TARGET="${2:?}"
SUBTARGET="${3:?}"
SDK_STAMP_DIR="${4:?}"
SDK_DOWNLOAD_RETRIES="${SDK_DOWNLOAD_RETRIES:-3}"
SDK_DOWNLOAD_RETRY_DELAY="${SDK_DOWNLOAD_RETRY_DELAY:-5}"

retry() {
    local attempts="${1:?attempt count required}"
    local delay="${2:?delay required}"
    shift 2
    local try=1
    while true; do
        if "$@"; then
            return 0
        fi
        if [ "$try" -ge "$attempts" ]; then
            return 1
        fi
        echo "[openwrt-sdk-setup] Command failed on attempt $try/$attempts: $*" >&2
        sleep "$delay"
        try=$((try + 1))
    done
}

STAMP="$SDK_STAMP_DIR/.stamp-sdk-ready"
if [ -f "$STAMP" ]; then
    echo "[openwrt-sdk-setup] SDK already ready at $SDK_STAMP_DIR"
    exit 0
fi

echo "[openwrt-sdk-setup] Discovering SDK for OpenWrt $VERSION $TARGET/$SUBTARGET ..."

BASE_URL="https://downloads.openwrt.org/releases/${VERSION}/targets/${TARGET}/${SUBTARGET}/"
SDK_FILE=$(VERSION="$VERSION" TARGET="$TARGET" SUBTARGET="$SUBTARGET" python3 - <<'PYEOF'
import os, sys, urllib.request, re
version  = os.environ["VERSION"]
target   = os.environ["TARGET"]
subtarget = os.environ["SUBTARGET"]
url = f"https://downloads.openwrt.org/releases/{version}/targets/{target}/{subtarget}/"
try:
    with urllib.request.urlopen(url) as r:
        html = r.read().decode("utf-8", errors="replace")
except Exception as e:
    sys.exit(f"ERROR: Could not fetch {url}: {e}")
m = re.search(r'href="(openwrt-sdk-[^"]+\.tar\.zst)"', html)
if not m:
    sys.exit(f"ERROR: No SDK tarball found at {url}")
print(m.group(1))
PYEOF
)

echo "[openwrt-sdk-setup] Downloading $SDK_FILE ..."
mkdir -p "$SDK_STAMP_DIR"
rm -f "$SDK_STAMP_DIR/$SDK_FILE"
retry "$SDK_DOWNLOAD_RETRIES" "$SDK_DOWNLOAD_RETRY_DELAY" \
    wget --show-progress --tries=1 --timeout=30 --waitretry=1 \
    -O "$SDK_STAMP_DIR/$SDK_FILE" "${BASE_URL}${SDK_FILE}" || {
        rm -f "$SDK_STAMP_DIR/$SDK_FILE" || true
        echo "[openwrt-sdk-setup] SDK download failed after $SDK_DOWNLOAD_RETRIES attempts." >&2
        exit 1
    }

echo "[openwrt-sdk-setup] Extracting SDK ..."
tar --zstd -xf "$SDK_STAMP_DIR/$SDK_FILE" -C "$SDK_STAMP_DIR"
rm "$SDK_STAMP_DIR/$SDK_FILE"

touch "$STAMP"
echo "[openwrt-sdk-setup] Done. SDK ready at $SDK_STAMP_DIR"
