#!/bin/bash

set -e -x

dump_errors() {
    echo "=== COMMAND FAILED: $1 ==="
    local dumps
    dumps=$(find logs/feeds/ -name "dump.txt" 2>/dev/null)
    if [ -n "$dumps" ]; then
        for f in $dumps; do
            echo "--- $f ---"
            cat "$f"
        done
    fi
    exit 1
}

run() {
    "$@" || dump_errors "$*"
}

# Install custom feed
cat /home/me/openwrt/feeds.conf.default > /home/me/openwrt/feeds.conf
cat /home/me/openwrt/owrt-packages/packages.feed >> /home/me/openwrt/feeds.conf

# Update feeds (clean/update may return non-zero on benign warnings)
./scripts/feeds clean || true
./scripts/feeds update -a || true
./scripts/feeds uninstall -a || true
run ./scripts/feeds install bzip2
run ./scripts/feeds install libcurl libnl-core libnl-route libmbedtls meson ninja
run ./scripts/feeds install -a -p custom -d m

run make defconfig

# Build packages
make -j$(nproc) || make -j1 V=s
