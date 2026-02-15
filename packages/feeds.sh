#!/bin/bash

set -e -x

# Install custom feed
cat /home/me/openwrt/feeds.conf.default > /home/me/openwrt/feeds.conf
cat /home/me/openwrt/owrt-packages/packages.feed >> /home/me/openwrt/feeds.conf

# Update feeds (clean/update may return non-zero on benign warnings)
./scripts/feeds clean
./scripts/feeds update -a
./scripts/feeds install -a -p custom -d m

# Verify critical base packages are present
find feeds/ -name "mbedtls" -type d | grep -q . || { echo "ERROR: mbedtls package not found in tree"; exit 1; }
find feeds/ -name "libnl" -type d | grep -q . || { echo "ERROR: libnl package not found in tree"; exit 1; }