#!/usr/bin/env bash

set -euo pipefail

PREFERRED_ROOT="${1:-/}"

is_sdk_root() {
    local candidate="$1"
    [ -d "$candidate" ] || return 1
    [ -f "$candidate/scripts/feeds" ] || return 1
    [ -d "$candidate/staging_dir" ] || return 1
}

maybe_emit() {
    local candidate="$1"
    if is_sdk_root "$candidate"; then
        printf '%s\n' "$candidate"
        exit 0
    fi
}

maybe_emit "$PREFERRED_ROOT"
maybe_emit "$PWD"
maybe_emit /builder
maybe_emit /home/build/openwrt
maybe_emit /openwrt

if [ -d "$PREFERRED_ROOT" ]; then
    while IFS= read -r candidate; do
        maybe_emit "$(dirname "$(dirname "$candidate")")"
    done < <(find "$PREFERRED_ROOT" -maxdepth 4 -type f -path '*/scripts/feeds' 2>/dev/null | sort)
fi

while IFS= read -r candidate; do
    maybe_emit "$(dirname "$(dirname "$candidate")")"
done < <(find / -maxdepth 4 -type f -path '*/scripts/feeds' 2>/dev/null | sort)

echo "[find-openwrt-sdk] Unable to locate an OpenWrt SDK root." >&2
exit 1
