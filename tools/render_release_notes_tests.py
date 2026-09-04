#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import unittest

from render_release_notes import previous_tag, render_release_notes, semantic_version


CHANGELOG = """## Unreleased

not released

## 0.2.0 - 2026-01-02

### Changes

1. current release

## 0.1.0 - 2025-01-01

1. initial release
"""


class ReleaseNotesTests(unittest.TestCase):
    def test_renders_only_selected_release_and_comparison(self) -> None:
        notes = render_release_notes(CHANGELOG, "0.2.0", "v0.1.0")
        self.assertIn("### Changes", notes)
        self.assertIn("compare/v0.1.0...v0.2.0", notes)
        self.assertNotIn("not released", notes)
        self.assertNotIn("initial release", notes)
        self.assertNotIn("## What's New\n\n\n", notes)

    def test_adds_highlights_and_source_link_for_initial_release(self) -> None:
        notes = render_release_notes(CHANGELOG, "0.1.0")
        self.assertIn("### Highlights", notes)
        self.assertIn("/tree/v0.1.0", notes)

    def test_rejects_missing_section(self) -> None:
        with self.assertRaisesRegex(ValueError, "0.3.0"):
            render_release_notes(CHANGELOG, "0.3.0")

    def test_validates_version_and_tag(self) -> None:
        for validator, invalid in ((semantic_version, "2.0"), (previous_tag, "0.2.0")):
            with self.assertRaises(argparse.ArgumentTypeError):
                validator(invalid)


if __name__ == "__main__":
    unittest.main()
