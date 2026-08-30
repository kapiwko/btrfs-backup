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
Usage: tools/render-release-notes.sh VERSION [PREVIOUS_TAG]

Render one released CHANGELOG.md section as GitHub release notes and append
the standard artifact footer. Omit PREVIOUS_TAG for the first release.
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
if [[ -n "$PREVIOUS_TAG" && ! "$PREVIOUS_TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
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
        content = content $0 ORS
        if ($0 ~ /^### /) {
            has_subheading = 1
        }
    }
    END {
        if (!has_subheading) {
            print "\n### Highlights"
        }
        printf "%s", content
    }
' "$ROOT/CHANGELOG.md"

cat <<EOF
## Artifacts

Download the package or archive appropriate for your system from the attached release assets. Verify downloaded files with \`SHA256SUMS\`; \`BUILD-REPORT.txt\` records the packaged version, target and test mode.
EOF
printf '\n'

if [[ -n "$PREVIOUS_TAG" ]]; then
    cat <<EOF
**Full changelog:** https://github.com/kapiwko/btrfs-backup/compare/$PREVIOUS_TAG...v$VERSION
EOF
else
    cat <<EOF
**Source at this release:** https://github.com/kapiwko/btrfs-backup/tree/v$VERSION
EOF
fi
