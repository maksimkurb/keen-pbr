#!/bin/bash

set -e -x

# Install custom feed (must run after source is copied)
./scripts/feeds install -a -p custom -d m

cp /home/me/openwrt/owrt-configs/${ROUTER_CONFIG}.config /home/me/openwrt/.config
echo "" >> /home/me/openwrt/.config
cat /home/me/openwrt/owrt-packages/openwrt/packages.config >> /home/me/openwrt/.config

make defconfig

# Build all dependencies with parallel jobs, then build our package with verbose output
#make -j$(nproc) || true
make package/keen-pbr3/compile "-j$(nproc)" V=s
