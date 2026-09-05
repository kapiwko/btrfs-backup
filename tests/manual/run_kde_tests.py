#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_kde  # noqa: E402


def subprocess_result(returncode: int):
    return type("Result", (), {"returncode": returncode})()


class ManualLabTests(unittest.TestCase):
    def test_connection_falls_back_to_session_libvirt(self) -> None:
        failed = subprocess_result(1)
        working = subprocess_result(0)
        with mock.patch.object(run_kde, "run", side_effect=[failed, working]) as command:
            selected = run_kde.select_connection(None)
        self.assertEqual("qemu:///session", selected)
        self.assertEqual("qemu:///system", command.call_args_list[0].args[0][2])

    def test_domain_uses_disposable_overlays_and_stable_disk_serials(self) -> None:
        with tempfile.TemporaryDirectory(prefix="manual-lab-test-") as directory:
            with mock.patch.dict("os.environ", {"MANUAL_QEMU_CACHE_DIR": directory, "PACKAGE_DIR": directory}):
                xml = run_kde.ManualLab("qemu:///session", viewer=False).xml()

        self.assertIn("<name>btrfs-backup-manual-lab</name>", xml)
        self.assertIn("root.qcow2", xml)
        self.assertIn("source.qcow2", xml)
        self.assertIn("target.qcow2", xml)
        self.assertIn("<serial>bb-manual-target</serial>", xml)
        self.assertIn("<serial>bb-manual-blank</serial>", xml)
        self.assertIn("<serial>bb-manual-partitioned</serial>", xml)
        self.assertIn("<serial>bb-manual-incompatible</serial>", xml)
        self.assertIn("<serial>bb-manual-adopt</serial>", xml)
        self.assertIn("<graphics type='spice'", xml)
        self.assertIn("<cpu mode='host-passthrough'", xml)
        self.assertIn("<rng model='virtio'>", xml)

    def test_domain_xml_escapes_cache_path(self) -> None:
        with tempfile.TemporaryDirectory(prefix="manual-&-lab-") as directory:
            with mock.patch.dict("os.environ", {"MANUAL_QEMU_CACHE_DIR": directory, "PACKAGE_DIR": directory}):
                xml = run_kde.ManualLab("qemu:///session", viewer=False).xml()
        self.assertIn("manual-&amp;-lab-", xml)

    def test_guest_failure_is_reported_before_controls_are_enabled(self) -> None:
        with tempfile.TemporaryDirectory(prefix="manual-lab-test-") as directory:
            console = Path(directory) / "current/console.log"
            console.parent.mkdir()
            console.write_text("MANUAL_LAB_STAGE saving-profile\nCOMMAND_FAILED profile create\n")
            with mock.patch.dict("os.environ", {"MANUAL_QEMU_CACHE_DIR": directory, "PACKAGE_DIR": directory}):
                lab = run_kde.ManualLab("qemu:///session", viewer=False)
                with self.assertRaisesRegex(RuntimeError, "guest setup failed"):
                    lab.wait_until_ready(timeout=0.1)


if __name__ == "__main__":
    unittest.main()
