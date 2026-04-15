#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <cmake-build-dir> [clang-tidy args...]" >&2
    exit 1
fi

BUILD_DIR="$1"
shift
COMPILE_COMMANDS="${BUILD_DIR}/compile_commands.json"

if [[ ! -f "${COMPILE_COMMANDS}" ]]; then
    echo "ERROR: compile_commands.json not found in ${BUILD_DIR}. Run the Clang configure step first." >&2
    exit 1
fi

CLANG_TIDY_BIN="${CLANG_TIDY_BIN:-}"
if [[ -z "${CLANG_TIDY_BIN}" ]]; then
    for candidate in clang-tidy-20 clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16 clang-tidy-15 clang-tidy-14 clang-tidy; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            CLANG_TIDY_BIN="${candidate}"
            break
        fi
    done
fi

if [[ -z "${CLANG_TIDY_BIN}" ]]; then
    echo "ERROR: clang-tidy was not found. Install clang-tidy and rerun this command." >&2
    exit 1
fi

mapfile -t FILES < <(rg --files src tests include -g '*.c' -g '*.cc' -g '*.cpp' -g '*.cxx' -g '*.h' -g '*.hpp' | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No project files matched for clang-tidy." >&2
    exit 0
fi

exec "${CLANG_TIDY_BIN}" -p "${BUILD_DIR}" "$@" "${FILES[@]}"
