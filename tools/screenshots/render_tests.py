#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import tempfile
import os
from pathlib import Path
import unittest
from unittest.mock import patch

from render import SCREENSHOT_TIME, deterministic_process_environment, scenario_specs, session_identity
from scenarios import NOTIFICATION_PANEL_SCRIPT, WIDGET_PANEL_SCRIPT
from session import DesktopSession

SCREENSHOT_MANAGER_SOURCE = (Path(__file__).parent / "kde-manager-demo.cpp").read_text()


class ScenarioSpecsTests(unittest.TestCase):
    def test_plasma_scripts_are_external_assets(self) -> None:
        self.assertIn('org.kde.plasma.notifications', NOTIFICATION_PANEL_SCRIPT)
        self.assertIn('org.btrfsbackup.plasmoid', WIDGET_PANEL_SCRIPT)
        self.assertIn('org.btrfsbackup.screenshotclock', NOTIFICATION_PANEL_SCRIPT)
        self.assertIn('org.btrfsbackup.screenshotclock', WIDGET_PANEL_SCRIPT)

    def test_all_contains_each_scenario_and_unique_outputs(self) -> None:
        specs = scenario_specs("all", "all")
        self.assertEqual({spec["kind"] for spec in specs}, {
            "active-window", "notification", "dolphin", "plasma-widget", "system-settings",
        })
        outputs = [spec["output"] for spec in specs]
        self.assertEqual(len(outputs), len(set(outputs)))
        self.assertEqual(len(outputs), 19)

    def test_filters_kcm_page(self) -> None:
        self.assertEqual(scenario_specs("kcm", "history"), [{
            "kind": "system-settings", "mode": "connected", "page": "history",
            "output": "system-settings-history.png",
        }])

        self.assertEqual(scenario_specs("kcm", "notification-settings"), [{
            "kind": "system-settings", "mode": "connected", "page": "notification-settings",
            "output": "system-settings-notification-settings.png",
        }])

    def test_filters_top_level_group(self) -> None:
        specs = scenario_specs("notifications", "all")
        self.assertEqual([spec["mode"] for spec in specs], ["transfer", "completion"])

    def test_dolphin_manager_supports_browse_operation_leases(self) -> None:
        self.assertIn("BeginBrowseOperation", SCREENSHOT_MANAGER_SOURCE)
        self.assertIn("EndBrowseOperation", SCREENSHOT_MANAGER_SOURCE)

    def test_session_identity_is_short_and_stable(self) -> None:
        name = "system-settings-new-profile-prepare-partition"
        self.assertEqual(session_identity(name), session_identity(name))
        self.assertEqual(len(session_identity(name)), 12)

    def test_deterministic_environment_freezes_wall_clock_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libfaketime.so.1"
            library.touch()
            with patch.dict(os.environ, {
                "BTRFS_BACKUP_SCREENSHOT_FAKETIME_LIBRARY": str(library),
                "LD_PRELOAD": "existing.so",
            }, clear=False):
                environment = deterministic_process_environment()
        self.assertEqual(environment["FAKETIME"], SCREENSHOT_TIME)
        self.assertEqual(environment["FAKETIME_DONT_FAKE_MONOTONIC"], "1")
        self.assertEqual(environment["LD_PRELOAD"], f"{library}:existing.so")
        self.assertEqual(environment["TZ"], "UTC")


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
