#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-}"
PREVIOUS_TAG="${2:-}"

usage() {
    cat <<'USAGE'
Usage: tools/render-release-notes.sh VERSION PREVIOUS_TAG

Render one released CHANGELOG.md section as GitHub release notes and append
the standard artifact and comparison footer.
USAGE
}

if [[ "$VERSION" == -h || "$VERSION" == --help ]]; then
    usage
    exit 0
fi
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf '%s\n' 'VERSION must use MAJOR.MINOR.PATCH format.' >&2
    usage >&2
    exit 2
fi
if [[ ! "$PREVIOUS_TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf '%s\n' 'PREVIOUS_TAG must use vMAJOR.MINOR.PATCH format.' >&2
    usage >&2
    exit 2
fi

section_heading="## $VERSION - "
if ! grep -Fq -- "$section_heading" "$ROOT/CHANGELOG.md"; then
    printf 'CHANGELOG.md has no released section for %s.\n' "$VERSION" >&2
    exit 1
fi

printf '%s\n' "## What's New"

awk -v heading="$section_heading" '
    index($0, heading) == 1 {
        in_release = 1
        next
    }
    in_release && /^## / {
        exit
    }
    in_release {
        print
    }
' "$ROOT/CHANGELOG.md"

cat <<EOF
## Artifacts

This release includes native Arch and Debian packages, the optional Plasma integration, a generic install archive, source archives and packaging skeletons. Verify downloaded files with the attached \`SHA256SUMS\`; \`BUILD-REPORT.txt\` records the packaged version, target and test mode.

**Full changelog:** https://github.com/kapiwko/btrfs-backup/compare/$PREVIOUS_TAG...v$VERSION
EOF
