#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build/clang-tidy}"
CLANG_TIDY_BIN="${CLANG_TIDY:-clang-tidy}"
RUN_CLANG_TIDY_BIN="${RUN_CLANG_TIDY:-run-clang-tidy}"

command -v cmake >/dev/null 2>&1 || {
    echo "error: cmake is required" >&2
    exit 2
}
command -v clang++ >/dev/null 2>&1 || {
    echo "error: clang++ is required" >&2
    exit 2
}
command -v "${CLANG_TIDY_BIN}" >/dev/null 2>&1 || {
    echo "error: ${CLANG_TIDY_BIN} is required" >&2
    exit 2
}
command -v "${RUN_CLANG_TIDY_BIN}" >/dev/null 2>&1 || {
    echo "error: ${RUN_CLANG_TIDY_BIN} is required" >&2
    exit 2
}

cmake \
    -S "${ROOT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_KDE_INTEGRATION=OFF

"${RUN_CLANG_TIDY_BIN}" \
    -clang-tidy-binary "${CLANG_TIDY_BIN}" \
    -p "${BUILD_DIR}" \
    -config-file "${ROOT_DIR}/.clang-tidy" \
    -header-filter "^${ROOT_DIR}/(apps|src)/" \
    -source-filter "^${ROOT_DIR}/(apps|src)/" \
    -j "${BUILD_JOBS:-$(nproc 2>/dev/null || echo 2)}" \
    -quiet
