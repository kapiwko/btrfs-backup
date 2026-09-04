#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import tempfile
from pathlib import Path
import unittest

from render import scenario_specs, session_identity
from scenarios import NOTIFICATION_PANEL_SCRIPT, WIDGET_PANEL_SCRIPT
from session import DesktopSession


class ScenarioSpecsTests(unittest.TestCase):
    def test_plasma_scripts_are_external_assets(self) -> None:
        self.assertIn('org.kde.plasma.notifications', NOTIFICATION_PANEL_SCRIPT)
        self.assertIn('org.btrfsbackup.plasmoid', WIDGET_PANEL_SCRIPT)

    def test_all_contains_each_scenario_and_unique_outputs(self) -> None:
        specs = scenario_specs("all", "all")
        self.assertEqual({spec["kind"] for spec in specs}, {
            "active-window", "notification", "dolphin", "plasma-widget", "system-settings",
        })
        outputs = [spec["output"] for spec in specs]
        self.assertEqual(len(outputs), len(set(outputs)))
        self.assertEqual(len(outputs), 18)

    def test_filters_kcm_page(self) -> None:
        self.assertEqual(scenario_specs("kcm", "history"), [{
            "kind": "system-settings", "mode": "connected", "page": "history",
            "output": "system-settings-history.png",
        }])

    def test_filters_top_level_group(self) -> None:
        specs = scenario_specs("notifications", "all")
        self.assertEqual([spec["mode"] for spec in specs], ["transfer", "completion"])

    def test_session_identity_is_short_and_stable(self) -> None:
        name = "system-settings-new-profile-prepare-partition"
        self.assertEqual(session_identity(name), session_identity(name))
        self.assertEqual(len(session_identity(name)), 12)


class DesktopSessionTests(unittest.TestCase):
    def test_cleanup_terminates_owned_processes(self) -> None:
        class Process:
            def __init__(self) -> None:
                self.terminated = False

            def poll(self) -> None:
                return None

            def terminate(self) -> None:
                self.terminated = True

            def wait(self, timeout: float) -> int:
                return 0

        with tempfile.TemporaryDirectory() as directory:
            session = DesktopSession(Path(directory))
            process = Process()
            session.processes.append(process)  # type: ignore[arg-type]
            session.__exit__(None, None, None)
            self.assertTrue(process.terminated)


if __name__ == "__main__":
    unittest.main()
