#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG_FORMAT_BIN="${CLANG_FORMAT:-clang-format}"
BASE_REF="${1:-${CPP_FORMAT_BASE:-}}"

command -v "${CLANG_FORMAT_BIN}" >/dev/null 2>&1 || {
    echo "error: ${CLANG_FORMAT_BIN} is required" >&2
    exit 2
}
command -v git-clang-format >/dev/null 2>&1 || {
    echo "error: git-clang-format is required" >&2
    exit 2
}

cd "${ROOT_DIR}"

if [[ "${BASE_REF}" =~ ^0+$ ]]; then
    BASE_REF=""
fi

if [[ -z "${BASE_REF}" && -n "${GITHUB_BASE_REF:-}" ]]; then
    BASE_REF="origin/${GITHUB_BASE_REF}"
fi

if [[ -n "${BASE_REF}" ]]; then
    git rev-parse --verify "${BASE_REF}^{commit}" >/dev/null
    BASE_REF="$(git merge-base HEAD "${BASE_REF}")"
elif [[ "${GITHUB_ACTIONS:-false}" == "true" ]] && git rev-parse --verify HEAD^ >/dev/null 2>&1; then
    BASE_REF="HEAD^"
else
    BASE_REF="HEAD"
fi

FORMAT_DIFF="$(mktemp)"
trap 'rm -f "${FORMAT_DIFF}"' EXIT

ADDED_FILES=()
while IFS= read -r -d '' path; do
    case "${path}" in
        *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx)
            ADDED_FILES+=("${path}")
            ;;
    esac
done < <(git diff --name-only --diff-filter=A -z "${BASE_REF}" -- apps src tests integrations/kde)

if (( ${#ADDED_FILES[@]} > 0 )); then
    "${CLANG_FORMAT_BIN}" --dry-run --Werror --style=file "${ADDED_FILES[@]}"
fi

git clang-format \
    --binary "${CLANG_FORMAT_BIN}" \
    --extensions c,cc,cpp,cxx,h,hh,hpp,hxx \
    --style file \
    --diff \
    "${BASE_REF}" \
    -- \
    apps src tests integrations/kde >"${FORMAT_DIFF}"

if grep -q '^diff --git ' "${FORMAT_DIFF}"; then
    echo "clang-format changes are required in modified C++ lines:" >&2
    cat "${FORMAT_DIFF}" >&2
    echo "Run: git clang-format ${BASE_REF} -- apps src tests integrations/kde" >&2
    exit 1
fi

echo "clang-format check passed (base: ${BASE_REF})"
