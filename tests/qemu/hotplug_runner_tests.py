#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from hotplug_vm import HotplugVm
from qmp_client import QmpClient, QmpError


class FakeProcess:
    def __init__(self, status: int | None = None) -> None:
        self.status = status
        self.terminated = False
        self.killed = False

    def poll(self) -> int | None:
        return self.status

    def send_signal(self, unused_signal: int) -> None:
        self.terminated = True
        self.status = 0

    def wait(self, timeout: float | None = None) -> int:
        return self.status or 0

    def kill(self) -> None:
        self.killed = True
        self.status = -9


def bare_vm(root: Path) -> HotplugVm:
    vm = HotplugVm.__new__(HotplugVm)
    vm.root = root
    vm.root_mount = root / "root"
    vm.root_mount.mkdir()
    vm.console_log = root / "console.log"
    vm.qmp_socket = root / "qmp.sock"
    vm.qemu = FakeProcess()
    vm.root_mounted = False
    vm.root_image_temporary = None
    return vm


class QmpClientTests(unittest.TestCase):
    def test_sends_capabilities_before_requested_command(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "qmp.sock"
            path.touch()
            stream = mock.Mock()
            stream.readline.side_effect = [b'{"QMP":{}}\n', b'{"return":{}}\n', b'{"return":{}}\n']
            connection = mock.MagicMock()
            connection.__enter__.return_value = connection
            connection.makefile.return_value = stream
            with mock.patch("qmp_client.socket.socket", return_value=connection):
                QmpClient(path).execute("device_del", {"id": "disk"})
            requests = [json.loads(call.args[0]) for call in stream.write.call_args_list]
            self.assertEqual(requests, [
                {"execute": "qmp_capabilities"},
                {"execute": "device_del", "arguments": {"id": "disk"}},
            ])

    def test_reports_qmp_error(self) -> None:
        stream = io.BytesIO(b'{"error":{"desc":"rejected"}}\n')
        with self.assertRaisesRegex(QmpError, "rejected"):
            QmpClient._read_response(stream)


class HotplugVmTests(unittest.TestCase):
    def test_wait_reports_guest_exit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            vm = bare_vm(Path(directory))
            vm.qemu = FakeProcess(7)
            with self.assertRaisesRegex(RuntimeError, "guest failed"):
                vm.wait_for("READY", "guest failed", timeout=0.01)

    def test_wait_reports_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            vm = bare_vm(Path(directory))
            with mock.patch("hotplug_vm.time.monotonic", side_effect=[0.0, 2.0]):
                with self.assertRaisesRegex(RuntimeError, "timed out"):
                    vm.wait_for("READY", "timed out", timeout=1.0)

    def test_scenario_drives_attach_detach_reset_and_reattach(self) -> None:
        vm = HotplugVm.__new__(HotplugVm)
        vm.wait_for = mock.Mock()
        vm.require = mock.Mock()
        vm.qmp = mock.Mock()
        vm.scenario()
        self.assertEqual([call.args[0] for call in vm.qmp.call_args_list],
                         ["device_add", "device_del", "device_add", "system_reset",
                          "device_add", "device_del", "device_add", "device_del", "device_add"])
        hotplug_drivers = [call.args[1]["driver"] for call in vm.qmp.call_args_list
                           if call.args[0] == "device_add" and call.args[1]["id"].startswith("target-")]
        self.assertEqual(hotplug_drivers, ["usb-storage", "nvme", "scsi-hd"])

    def test_close_stops_qemu_and_removes_artifacts(self) -> None:
        root = Path(tempfile.mkdtemp())
        vm = bare_vm(root)
        process = vm.qemu
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("QEMU_KEEP_ARTIFACTS", None)
            vm.close()
        self.assertTrue(process.terminated)
        self.assertFalse(root.exists())

    def test_close_can_preserve_artifacts(self) -> None:
        root = Path(tempfile.mkdtemp())
        vm = bare_vm(root)
        with mock.patch.dict(os.environ, {"QEMU_KEEP_ARTIFACTS": "1"}):
            vm.close()
        self.assertTrue(root.exists())
        vm.root_mount.rmdir()
        root.rmdir()

    def test_run_cleans_up_after_exception(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            vm = bare_vm(Path(directory))
            vm.validate = mock.Mock(side_effect=RuntimeError("broken setup"))
            vm.close = mock.Mock()
            with self.assertRaisesRegex(RuntimeError, "broken setup"):
                vm.run()
            vm.close.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
