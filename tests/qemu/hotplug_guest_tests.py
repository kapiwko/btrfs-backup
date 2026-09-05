#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import json
import inspect
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import hotplug_guest


class HotplugGuestTests(unittest.TestCase):
    def test_generated_counter_is_valid_python(self) -> None:
        compile(hotplug_guest.COUNTER_PROGRAM, "qemu-hotplug-counter", "exec")

    def test_transaction_reads_structured_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            transactions = Path(directory)
            (transactions / "operation.json").write_text('{"state":"succeeded"}\n')
            with mock.patch.object(hotplug_guest, "TRANSACTIONS", transactions):
                self.assertEqual(hotplug_guest.transaction("operation")["state"], "succeeded")

    def test_operation_id_rejects_missing_value(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            response = Path(directory) / "response.json"
            response.write_text('{"state":"running"}\n')
            with self.assertRaisesRegex(RuntimeError, "missing operationId"):
                hotplug_guest.operation_id_from(response)

    def test_require_document_accepts_recovery_error_and_cleanup(self) -> None:
        with mock.patch.object(hotplug_guest, "transaction", return_value={
            "state": "interrupted",
            "errorCode": "device-preparation.daemon-restarted",
            "cleanupResult": "mapper-closed",
        }):
            hotplug_guest.require_document(
                "operation",
                state="interrupted",
                error_code=hotplug_guest.re.compile(
                    r"device-preparation\.(daemon-restarted|helper-exited)"),
                cleanup_result=("mapper-closed", "not-required"),
            )

    def test_execute_validates_configuration_before_running_commands(self) -> None:
        with mock.patch.object(hotplug_guest, "run") as run:
            with self.assertRaisesRegex(ValueError, "invalid values"):
                hotplug_guest.execute({"target_uuid": "only-one-value"})
        run.assert_not_called()

    def test_execute_preserves_scenario_order(self) -> None:
        events: list[str] = []
        config = {
            "target_uuid": "uuid",
            "target_device_unit": "dev-test.device",
            "replacement_hash": "digest",
        }
        with (
            mock.patch.object(hotplug_guest, "run", side_effect=lambda *args, **kwargs: events.append("package")),
            mock.patch.object(hotplug_guest, "recover_power_loss", side_effect=lambda: events.append("recovery") or False),
            mock.patch.object(hotplug_guest, "configure_hotplug", side_effect=lambda *args: events.append("hotplug")),
            mock.patch.object(hotplug_guest, "prepare_source", side_effect=lambda: events.append("source")),
            mock.patch.object(hotplug_guest, "systemctl", side_effect=lambda *args: events.append("daemon")),
            mock.patch.object(hotplug_guest, "verify_initial_provisioning", side_effect=lambda: events.append("provision")),
            mock.patch.object(hotplug_guest, "verify_manager_recovery", side_effect=lambda: events.append("manager")),
            mock.patch.object(hotplug_guest, "verify_helper_recovery", side_effect=lambda: events.append("helper")),
            mock.patch.object(hotplug_guest, "verify_unplug", side_effect=lambda *args: events.append("unplug")),
            mock.patch.object(hotplug_guest, "begin_power_loss", side_effect=lambda: events.append("power-loss")),
        ):
            hotplug_guest.execute(config)
        self.assertEqual(events, [
            "package", "recovery", "hotplug", "source", "daemon", "provision",
            "manager", "helper", "unplug", "power-loss",
        ])

    def test_cached_setup_unit_invokes_python_guest(self) -> None:
        from hotplug_vm import ROOTFS_CACHE_VERSION, SETUP_UNIT

        self.assertIn("/usr/bin/python3 /run/qemu-test-setup/hotplug_guest.py", SETUP_UNIT)
        self.assertNotIn("/usr/bin/sh", SETUP_UNIT)
        self.assertEqual(ROOTFS_CACHE_VERSION, "v3")

    def test_guest_keeps_markers_consumed_by_host_scenario(self) -> None:
        from hotplug_vm import HotplugVm

        guest_source = inspect.getsource(hotplug_guest)
        host_source = inspect.getsource(HotplugVm.scenario)
        host_markers = set(hotplug_guest.re.findall(r'"(QEMU_[A-Z0-9_]+)"', host_source))
        dynamic_markers = {"QEMU_HOTPLUG_OK_1", "QEMU_HOTPLUG_OK_2",
                           "QEMU_TARGET_STOP_1", "QEMU_TARGET_START_2"}
        self.assertFalse(host_markers - dynamic_markers - set(
            hotplug_guest.re.findall(r'"(QEMU_[A-Z0-9_]+)"', guest_source)))

    def test_guest_configuration_is_json_serializable(self) -> None:
        document = {
            "target_uuid": "uuid with spaces",
            "target_device_unit": "dev-disk-by\\x2duuid-test.device",
            "replacement_hash": "deadbeef",
        }
        self.assertEqual(json.loads(json.dumps(document)), document)


if __name__ == "__main__":
    unittest.main()
