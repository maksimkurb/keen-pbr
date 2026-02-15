#!/bin/bash

set -e -x

cp /home/me/openwrt/owrt-configs/${ROUTER_CONFIG}.config /home/me/openwrt/.config
echo "" >> /home/me/openwrt/.config
cat /home/me/openwrt/owrt-packages/packages.config >> /home/me/openwrt/.config

make defconfig

# Build all dependencies with parallel jobs, then build our package with verbose output
#make -j$(nproc) || true
make package/keen-pbr3/compile "-j$(nproc)" V=s
