#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
TAG_PATTERN = re.compile(r"^v[0-9]+\.[0-9]+\.[0-9]+$")
ARTIFACTS = """## Artifacts

Download the package or archive appropriate for your system from the attached release assets. Verify downloaded files with `SHA256SUMS`; `BUILD-REPORT.txt` records the packaged version, target and test mode.
"""


def semantic_version(value: str) -> str:
    if not VERSION_PATTERN.fullmatch(value):
        raise argparse.ArgumentTypeError("VERSION must use MAJOR.MINOR.PATCH format.")
    return value


def previous_tag(value: str) -> str:
    if not TAG_PATTERN.fullmatch(value):
        raise argparse.ArgumentTypeError("PREVIOUS_TAG must use vMAJOR.MINOR.PATCH format.")
    return value


def released_section(changelog: str, version: str) -> list[str]:
    heading = f"## {version} - "
    lines = changelog.splitlines()
    try:
        start = next(index for index, line in enumerate(lines) if line.startswith(heading)) + 1
    except StopIteration as error:
        raise ValueError(f"CHANGELOG.md has no released section for {version}.") from error
    end = next((index for index in range(start, len(lines)) if lines[index].startswith("## ")), len(lines))
    return lines[start:end]


def render_release_notes(changelog: str, version: str, previous: str | None = None) -> str:
    section = released_section(changelog, version)
    while section and section[0] == "":
        section.pop(0)
    while section and section[-1] == "":
        section.pop()
    parts = ["## What's New", ""]
    if not any(line.startswith("### ") for line in section):
        parts += ["### Highlights", ""]
    parts += section
    parts += ["", ARTIFACTS.rstrip(), ""]
    if previous:
        parts.append(f"**Full changelog:** https://github.com/kapiwko/btrfs-backup/compare/{previous}...v{version}")
    else:
        parts.append(f"**Source at this release:** https://github.com/kapiwko/btrfs-backup/tree/v{version}")
    return "\n".join(parts) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render one released CHANGELOG.md section as GitHub release notes."
    )
    parser.add_argument("version", type=semantic_version, metavar="VERSION")
    parser.add_argument("previous_tag", nargs="?", type=previous_tag, metavar="PREVIOUS_TAG")
    arguments = parser.parse_args()
    try:
        notes = render_release_notes((ROOT / "CHANGELOG.md").read_text(), arguments.version, arguments.previous_tag)
    except ValueError as error:
        parser.exit(1, f"{error}\n")
    print(notes, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
