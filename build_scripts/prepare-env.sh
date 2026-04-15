#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -f "${ROOT_DIR}/CMakeLists.txt" ]]; then
    echo "ERROR: expected keen-pbr repo root next to this script." >&2
    exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
    echo "ERROR: this bootstrap script currently supports Debian/Ubuntu environments with apt-get." >&2
    exit 1
fi

APT_RUN=()
if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "ERROR: sudo is required when running as a non-root user." >&2
        exit 1
    fi
    APT_RUN=(sudo)
fi

COMMON_PACKAGES=(
    bash
    build-essential
    ca-certificates
    cmake
    curl
    file
    git
    make
    ninja-build
    pkg-config
    rsync
    unzip
    xz-utils
    zstd
)

DEV_PACKAGES=(
    libcurl4-openssl-dev
    libfmt-dev
    libnl-3-dev
    libnl-route-3-dev
    nlohmann-json3-dev
)

COMPILER_PACKAGES=(gcc g++)
if apt-cache show g++-13 >/dev/null 2>&1; then
    COMPILER_PACKAGES=(gcc-13 g++-13)
fi

ANALYSIS_PACKAGES=(
    clang
    clang-tidy
)

echo "==> Installing native build dependencies"
"${APT_RUN[@]}" apt-get update
"${APT_RUN[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    "${COMMON_PACKAGES[@]}" \
    "${DEV_PACKAGES[@]}" \
    "${ANALYSIS_PACKAGES[@]}" \
    "${COMPILER_PACKAGES[@]}"

if ! command -v bun >/dev/null 2>&1; then
    echo "==> Installing bun"
    export BUN_INSTALL="${BUN_INSTALL:-${HOME}/.bun}"
    export PATH="${BUN_INSTALL}/bin:${PATH}"
    curl -fsSL https://bun.sh/install | bash
else
    echo "==> bun already installed"
fi

export BUN_INSTALL="${BUN_INSTALL:-${HOME}/.bun}"
export PATH="${BUN_INSTALL}/bin:${PATH}"

if git -C "${ROOT_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "==> Initializing git submodules"
    git -C "${ROOT_DIR}" submodule update --init --recursive
fi

if [[ -f "${ROOT_DIR}/frontend/package.json" ]]; then
    echo "==> Installing frontend dependencies with bun"
    (
        cd "${ROOT_DIR}/frontend"
        bun install --frozen-lockfile
    )
fi

echo
echo "Environment is ready."
echo "Next steps:"
echo "  cd ${ROOT_DIR}"
echo "  make"
echo "  make test"
echo "  make clang-build"
echo "  make clang-check"
echo "  make clang-tidy"
