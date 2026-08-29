#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${CLANG_TIDY_BUILD_DIR:-${ROOT_DIR}/build/clang-tidy}"
BASE_REF="${CLANG_TIDY_BASE:-HEAD}"
CLANG_TIDY_BIN="${CLANG_TIDY:-clang-tidy}"
BUILD_JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || echo 2)}"

find_clang_tidy_diff() {
    local candidate

    if [[ -n "${CLANG_TIDY_DIFF:-}" ]]; then
        printf '%s\n' "$CLANG_TIDY_DIFF"
        return
    fi
    if command -v clang-tidy-diff.py >/dev/null 2>&1; then
        command -v clang-tidy-diff.py
        return
    fi
    for candidate in /usr/share/clang/clang-tidy-diff.py /usr/lib/llvm*/share/clang/clang-tidy-diff.py; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return
        fi
    done

    return 1
}

for command_name in cmake clang++ git python3 "$CLANG_TIDY_BIN"; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "error: ${command_name} is required" >&2
        exit 2
    }
done

git -C "$ROOT_DIR" rev-parse --verify --quiet "${BASE_REF}^{commit}" >/dev/null || {
    echo "error: clang-tidy base is not a commit: ${BASE_REF}" >&2
    exit 2
}

CLANG_TIDY_DIFF_BIN="$(find_clang_tidy_diff)" || {
    echo "error: clang-tidy-diff.py is required" >&2
    exit 2
}

cmake \
    -S "$ROOT_DIR" \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_KDE_INTEGRATION=OFF \
    -DBUILD_TESTING=OFF >/dev/null

git -C "$ROOT_DIR" diff --no-ext-diff --unified=0 "$BASE_REF" -- apps src \
    | python3 "$CLANG_TIDY_DIFF_BIN" \
        -clang-tidy-binary "$CLANG_TIDY_BIN" \
        -p 1 \
        -regex '^(apps|src)/.*\.(c|cc|cpp|cxx)$' \
        -path "$BUILD_DIR" \
        -config-file "$ROOT_DIR/.clang-tidy" \
        -j "$BUILD_JOBS" \
        -quiet \
        -only-check-in-db
