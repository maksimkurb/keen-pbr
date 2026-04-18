#!/bin/sh

set -eu

WORKSPACE="${1:?Usage: $0 <workspace-dir> [output-dir]}"
OUTPUT_DIR="${2:-$WORKSPACE/frontend/dist}"

ensure_bun() {
    if command -v bun >/dev/null 2>&1; then
        return
    fi

    if [ -x /root/.bun/bin/bun ]; then
        export BUN_INSTALL=/root/.bun
        export PATH="$BUN_INSTALL/bin:$PATH"
        return
    fi

    if ! command -v curl >/dev/null 2>&1; then
        if command -v apt-get >/dev/null 2>&1; then
            apt-get update
            DEBIAN_FRONTEND=noninteractive apt-get install -y ca-certificates curl unzip
            rm -rf /var/lib/apt/lists/*
        else
            echo "ERROR: bun is not installed and curl is unavailable for bootstrap." >&2
            exit 1
        fi
    fi

    export BUN_INSTALL=/root/.bun
    export PATH="$BUN_INSTALL/bin:$PATH"
    curl -fsSL https://bun.sh/install | sh
}

ensure_bun

TMP_ROOT="${TMPDIR:-/tmp}/keen-pbr-bun"
TMP_BUILD_DIR="$(mktemp -d "${TMP_ROOT}.dist.XXXXXX")"
trap 'rm -rf "$TMP_BUILD_DIR"' EXIT

mkdir -p "${TMP_ROOT}" "$OUTPUT_DIR"

cd "$WORKSPACE/frontend"
TMPDIR="${TMP_ROOT}" TEMP="${TMP_ROOT}" TMP="${TMP_ROOT}" BUN_INSTALL_CACHE_DIR="${TMP_ROOT}/cache" bun install --frozen-lockfile
TMPDIR="${TMP_ROOT}" TEMP="${TMP_ROOT}" TMP="${TMP_ROOT}" BUN_INSTALL_CACHE_DIR="${TMP_ROOT}/cache" \
    KEEN_PBR_FRONTEND_OUT_DIR="$TMP_BUILD_DIR" bun run build

mkdir -p "$OUTPUT_DIR"
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
cp -a "$TMP_BUILD_DIR"/. "$OUTPUT_DIR"/
