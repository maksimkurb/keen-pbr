#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <cmake-build-dir> [clangd-tidy args...]" >&2
    exit 1
fi

BUILD_DIR="$1"
shift
COMPILE_COMMANDS="${BUILD_DIR}/compile_commands.json"

if [[ ! -f "${COMPILE_COMMANDS}" ]]; then
    echo "ERROR: compile_commands.json not found in ${BUILD_DIR}. Run the Clang configure step first." >&2
    exit 1
fi

CLANGD_BIN="${CLANGD_BIN:-}"
if [[ -z "${CLANGD_BIN}" ]]; then
    for candidate in clangd-20 clangd-19 clangd-18 clangd-17 clangd-16 clangd-15 clangd-14 clangd; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            CLANGD_BIN="${candidate}"
            break
        fi
    done
fi

if [[ -z "${CLANGD_BIN}" ]]; then
    echo "ERROR: clangd was not found. Install clangd and rerun this command." >&2
    exit 1
fi

CLANGD_TIDY_BIN="${CLANGD_TIDY_BIN:-}"
if [[ -z "${CLANGD_TIDY_BIN}" ]]; then
    for candidate in clangd-tidy "${HOME}/.local/bin/clangd-tidy"; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            CLANGD_TIDY_BIN="${candidate}"
            break
        fi
        if [[ -x "${candidate}" ]]; then
            CLANGD_TIDY_BIN="${candidate}"
            break
        fi
    done
fi

if [[ -z "${CLANGD_TIDY_BIN}" ]]; then
    echo "ERROR: clangd-tidy was not found. Install clangd-tidy and rerun this command." >&2
    exit 1
fi

mapfile -t FILES < <(
    find src include -type f \
        \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hpp' \) \
        ! -path 'include/keen-pbr/thread_annotations.hpp' \
        | sort
)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No project files matched for clangd-tidy." >&2
    exit 0
fi

exec "${CLANGD_TIDY_BIN}" \
    --compile-commands-dir "${BUILD_DIR}" \
    --clangd-executable "${CLANGD_BIN}" \
    "$@" \
    "${FILES[@]}"
