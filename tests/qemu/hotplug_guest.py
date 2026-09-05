#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from typing import Callable


SERIAL = Path("/dev/ttyS0")
SETUP = Path("/run/qemu-test-setup")
TRANSACTIONS = Path("/var/lib/btrfs-backup/device-preparations")
POWER_LOSS_MARKER = Path("/var/lib/btrfs-backup/qemu-power-loss-operation")
SOURCE = Path("/mnt/qemu-provisioning-source/home")
KEY = SETUP / "provisioning.key"
CLIENT = SETUP / "device-provisioning-client"
COUNTER_PROGRAM = """#!/usr/bin/env python3
import pathlib
import subprocess
import sys

kind, action = sys.argv[1:]
if kind == "backup" and subprocess.run(
        ["systemctl", "is-active", "--quiet", "graphical.target"]).returncode == 0:
    raise SystemExit(1)
counter = pathlib.Path(f"/run/qemu-hotplug-{kind}-count")
count = int(counter.read_text()) if counter.is_file() else 0
if action == "start":
    count += 1
    counter.write_text(f"{count}\\n")
prefix = "HOTPLUG_OK" if kind == "backup" else kind.upper()
marker = f"QEMU_{prefix}_{count}" if kind == "backup" else f"QEMU_{prefix}_{action.upper()}_{count}"
with pathlib.Path("/dev/ttyS0").open("a") as stream:
    print(marker, file=stream, flush=True)
"""


def run(arguments: list[str], *, capture: bool = False, output: Path | None = None,
        check: bool = True) -> subprocess.CompletedProcess[str]:
    destination = output.open("w") if output is not None else None
    try:
        return subprocess.run(
            arguments,
            check=check,
            text=True,
            capture_output=capture,
            stdout=destination,
            env={**os.environ, "LC_ALL": "C"},
        )
    finally:
        if destination is not None:
            destination.close()


def serial(marker: str) -> None:
    with SERIAL.open("a") as stream:
        print(marker, file=stream, flush=True)


def transaction_file(operation: str) -> Path:
    return TRANSACTIONS / f"{operation}.json"


def transaction(operation: str) -> dict[str, object]:
    return json.loads(transaction_file(operation).read_text())


def wait_until(predicate: Callable[[], bool], attempts: int, delay: float) -> bool:
    for _ in range(attempts):
        if predicate():
            return True
        time.sleep(delay)
    return False


def wait_for_transaction_state(operation: str, expected: str) -> None:
    def ready() -> bool:
        try:
            return transaction(operation).get("state") == expected
        except (FileNotFoundError, json.JSONDecodeError):
            return False

    if not wait_until(ready, 1800, 0.1):
        raise RuntimeError(f"transaction {operation} did not reach {expected}")


def wait_for_helper_pid(unit: str) -> int:
    def current_pid() -> int:
        result = run(["systemctl", "show", "--property=MainPID", "--value", unit],
                     capture=True, check=False)
        value = result.stdout.strip()
        return int(value) if value.isdigit() else 0

    for _ in range(600):
        if pid := current_pid():
            return pid
        time.sleep(0.05)
    raise RuntimeError(f"helper did not start: {unit}")


def operation_id_from(path: Path) -> str:
    value = json.loads(path.read_text()).get("operationId")
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"missing operationId in {path}")
    return value


def refresh_preparation_status(operation: str) -> None:
    run([
        "busctl", "--system", "call", "io.github.btrfsbackup.Manager1",
        "/io/github/btrfsbackup/Manager1", "io.github.btrfsbackup.Manager1",
        "GetDevicePreparation", "s", operation,
    ], check=False)


def require_document(operation: str, *, state: str | tuple[str, ...] | None = None,
                     error_code: str | re.Pattern[str] | None = None,
                     cleanup_result: tuple[str, ...] | None = None) -> None:
    document = transaction(operation)
    if state is not None:
        expected = (state,) if isinstance(state, str) else state
        if document.get("state") not in expected:
            raise RuntimeError(f"unexpected transaction state: {document}")
    if error_code is not None:
        actual = document.get("errorCode")
        matches = actual == error_code if isinstance(error_code, str) else (
            isinstance(actual, str) and error_code.fullmatch(actual) is not None)
        if not matches:
            raise RuntimeError(f"unexpected transaction error: {document}")
    if cleanup_result is not None and document.get("cleanupResult") not in cleanup_result:
        raise RuntimeError(f"unexpected cleanup result: {document}")


def device_exists(path: str) -> bool:
    return Path(path).exists()


def systemctl(*arguments: str, capture: bool = False, check: bool = True) -> subprocess.CompletedProcess[str]:
    return run(["systemctl", *arguments], capture=capture, check=check)


def stop_helper(unit: str) -> None:
    systemctl("kill", "--kill-whom=main", "--signal=STOP", unit)


def probe_helper_device_isolation(unit: str, selected: str, unrelated: str) -> None:
    gate = Path(f"/run/qemu-device-policy-gate-{os.getpid()}")
    output = Path(f"/run/qemu-device-policy-output-{os.getpid()}")
    os.mkfifo(gate, 0o600)
    child = os.fork()
    if child == 0:
        try:
            with gate.open("r") as stream:
                if stream.readline().strip() != "go":
                    os._exit(2)
            with open(selected, "rb", buffering=0) as source, output.open("wb") as destination:
                destination.write(source.read(4096))
            try:
                with open(unrelated, "rb", buffering=0) as source:
                    source.read(4096)
            except OSError:
                os._exit(0)
            os._exit(3)
        except BaseException:
            os._exit(4)
    try:
        control_group = systemctl("show", "--property=ControlGroup", "--value", unit,
                                  capture=True).stdout.strip()
        if not control_group:
            raise RuntimeError(f"missing control group for {unit}")
        Path(f"/sys/fs/cgroup{control_group}/cgroup.procs").write_text(f"{child}\n")
        gate.write_text("go\n")
        _, status = os.waitpid(child, 0)
        if os.waitstatus_to_exitcode(status) != 0:
            raise RuntimeError("helper device-isolation probe failed")
    finally:
        gate.unlink(missing_ok=True)
        output.unlink(missing_ok=True)


def install_text(path: Path, text: str, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(mode)


def configure_hotplug(target_uuid: str, target_device_unit: str) -> None:
    Path("/etc/btrfs-backup").mkdir(parents=True, exist_ok=True)
    install_text(Path("/etc/udev/rules.d/99-btrfs-backup-default.rules"),
                 f'ACTION=="add", SUBSYSTEM=="block", ENV{{ID_FS_TYPE}}=="crypto_LUKS", '
                 f'ENV{{ID_FS_UUID}}=="{target_uuid}", TAG+="systemd", '
                 'ENV{SYSTEMD_WANTS}+="btrfs-backup@default.service"\n')
    install_text(Path("/usr/local/bin/qemu-hotplug-counter"), COUNTER_PROGRAM, 0o755)
    install_text(Path("/etc/systemd/system/btrfs-backup@default.service.d/qemu-hotplug-test.conf"), """[Unit]
OnSuccess=
OnFailure=
Requires=qemu-hotplug-target-holder.service
After=qemu-hotplug-target-holder.service

[Service]
ExecStart=
ExecStart=/usr/local/bin/qemu-hotplug-counter backup start
""")
    install_text(Path("/etc/systemd/system/btrfs-backup-target@default.service.d/qemu-hotplug-test.conf"), """[Service]
ExecStart=
ExecStart=/usr/local/bin/qemu-hotplug-counter target start
ExecStop=
ExecStop=/usr/local/bin/qemu-hotplug-counter target stop
""")
    install_text(Path("/etc/systemd/system/qemu-hotplug-target-holder.service"), f"""[Unit]
Description=Hold target activation while the QEMU removable device exists
BindsTo={target_device_unit}
After={target_device_unit} btrfs-backup-target@default.service
Requires=btrfs-backup-target@default.service

[Service]
Type=oneshot
ExecStart=/usr/bin/true
RemainAfterExit=yes
""")
    systemctl("daemon-reload")
    run(["udevadm", "control", "--reload"])
    if run(["systemd-detect-virt", "--container"], check=False).returncode == 0:
        raise RuntimeError("QEMU guest was detected as a container")
    if systemctl("is-active", "--quiet", "graphical.target", check=False).returncode == 0:
        raise RuntimeError("graphical target must remain inactive")


def provision(device: str, profile: str, mode: str = "erase-whole-device",
              *, start_only: bool = False) -> Path:
    output = Path(f"/run/{profile}.json")
    arguments = [str(CLIENT), device, str(SOURCE), str(KEY), mode, profile]
    if start_only:
        arguments.append("start-only")
    run(arguments, output=output)
    return output


def sha256(path: str | Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def recover_power_loss() -> bool:
    if not POWER_LOSS_MARKER.is_file():
        return False
    operation = POWER_LOSS_MARKER.read_text().strip()
    systemctl("start", "polkit.service", "btrfs-backupd.service")
    wait_for_transaction_state(operation, "interrupted")
    require_document(operation,
                     error_code=re.compile(r"device-preparation\.(daemon-restarted|helper-exited)"))
    if not wait_until(lambda: not device_exists("/dev/mapper/btrfs-backup-qemu-power-loss"), 600, 0.1):
        raise RuntimeError("power-loss mapper remained active")
    require_document(operation, cleanup_result=("mapper-closed", "not-required"))
    POWER_LOSS_MARKER.unlink()
    serial("QEMU_POWER_LOSS_RECOVERED")
    serial("QEMU_READY")
    return True


def prepare_source() -> None:
    run(["udevadm", "settle", "--timeout=30"])
    Path("/mnt/qemu-provisioning-source").mkdir(parents=True, exist_ok=True)
    run(["mount", "/dev/vdc", "/mnt/qemu-provisioning-source"])
    run(["btrfs", "subvolume", "create", str(SOURCE)])
    run(["mount", "--bind", str(SOURCE), str(SOURCE)])
    (SOURCE.parent / ".snapshots/home").mkdir(parents=True, mode=0o700, exist_ok=True)


def verify_initial_provisioning() -> None:
    whole = provision("/dev/vdd", "qemu-whole-device")
    if run(["blkid", "-s", "PTTYPE", "-o", "value", "/dev/vdd"], capture=True).stdout.strip() != "gpt":
        raise RuntimeError("whole-device provisioning did not create GPT")
    if not device_exists("/dev/vdd1"):
        raise RuntimeError("whole-device provisioning did not create a partition")
    if run(["blkid", "-s", "TYPE", "-o", "value", "/dev/vdd1"], capture=True).stdout.strip() != "crypto_LUKS":
        raise RuntimeError("whole-device partition is not LUKS")
    if json.loads(whole.read_text()).get("state") != "succeeded":
        raise RuntimeError("whole-device provisioning failed")
    if not Path("/etc/btrfs-backup/profiles/qemu-whole-device/profile.json").is_file():
        raise RuntimeError("whole-device profile was not saved")

    before = Path("/run/qemu-partition-table-before")
    after = Path("/run/qemu-partition-table-after")
    run(["sfdisk", "--dump", "/dev/vde"], output=before)
    sibling_hash = sha256("/dev/vde1")
    existing = provision("/dev/vde2", "qemu-existing-partition", "reformat-existing-partition")
    run(["sfdisk", "--dump", "/dev/vde"], output=after)
    if sibling_hash != sha256("/dev/vde1") or before.read_bytes() != after.read_bytes():
        raise RuntimeError("existing-partition provisioning modified the parent layout or sibling")
    if run(["blkid", "-s", "TYPE", "-o", "value", "/dev/vde2"], capture=True).stdout.strip() != "crypto_LUKS":
        raise RuntimeError("existing partition is not LUKS")
    if json.loads(existing.read_text()).get("state") != "succeeded":
        raise RuntimeError("existing-partition provisioning failed")
    if not Path("/etc/btrfs-backup/profiles/qemu-existing-partition/profile.json").is_file():
        raise RuntimeError("existing-partition profile was not saved")


def verify_manager_recovery() -> None:
    operation = operation_id_from(provision("/dev/vdf", "qemu-manager-kill", start_only=True))
    unit = f"btrfs-backup-device-preparation@{operation}.service"
    wait_for_helper_pid(unit)
    stop_helper(unit)
    manager_pid = int(systemctl("show", "--property=MainPID", "--value", "btrfs-backupd.service",
                                capture=True).stdout.strip())
    os.kill(manager_pid, signal.SIGKILL)
    wait_until(lambda: systemctl("is-active", "--quiet", "btrfs-backupd.service",
                                 check=False).returncode != 0, 300, 0.05)
    systemctl("reset-failed", "btrfs-backupd.service")
    systemctl("start", "btrfs-backupd.service")
    systemctl("kill", "--kill-whom=main", "--signal=CONT", unit)
    wait_for_transaction_state(operation, "succeeded")
    if not Path("/etc/btrfs-backup/profiles/qemu-manager-kill/profile.json").is_file():
        raise RuntimeError("manager-recovery profile was not saved")
    serial("QEMU_MANAGER_KILL_OK")


def verify_helper_recovery() -> None:
    operation = operation_id_from(provision("/dev/vdg", "qemu-helper-kill", start_only=True))
    unit = f"btrfs-backup-device-preparation@{operation}.service"
    wait_for_helper_pid(unit)
    stop_helper(unit)
    probe_helper_device_isolation(unit, "/dev/vdg", "/dev/vdf")
    device_allow = systemctl("show", "--property=DeviceAllow", "--value", unit,
                             capture=True).stdout
    if re.search(r"block-[^ ]|block \*:", device_allow):
        raise RuntimeError(f"helper received a broad DeviceAllow rule: {device_allow}")
    serial("QEMU_DEVICE_ISOLATION_OK")
    systemctl("kill", "--kill-whom=all", "--signal=KILL", unit)
    for _ in range(600):
        refresh_preparation_status(operation)
        try:
            if transaction(operation).get("state") == "interrupted":
                break
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(0.1)
    wait_for_transaction_state(operation, "interrupted")
    require_document(operation, error_code="device-preparation.helper-exited")
    serial("QEMU_HELPER_KILL_OK")


def verify_unplug(replacement_hash: str) -> None:
    serial("QEMU_UNPLUG_ATTACH_READY")
    if not wait_until(lambda: device_exists("/dev/vdi"), 600, 0.1):
        raise RuntimeError("provisioning hotplug disk did not appear")
    operation = operation_id_from(provision("/dev/vdi", "qemu-device-unplug", start_only=True))
    unit = f"btrfs-backup-device-preparation@{operation}.service"
    wait_for_helper_pid(unit)
    stop_helper(unit)
    serial("QEMU_UNPLUG_READY")
    if not wait_until(lambda: not device_exists("/dev/vdi"), 600, 0.1):
        raise RuntimeError("provisioning hotplug disk did not disappear")
    systemctl("kill", "--kill-whom=main", "--signal=CONT", unit, check=False)
    for _ in range(600):
        refresh_preparation_status(operation)
        try:
            if transaction(operation).get("state") in ("failed", "interrupted"):
                break
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(0.1)
    require_document(operation, state=("failed", "interrupted"),
                     error_code=re.compile(r"device-preparation\..+"))
    serial("QEMU_REPLACEMENT_ATTACH_READY")
    if not wait_until(lambda: device_exists("/dev/vdi"), 600, 0.1):
        raise RuntimeError("replacement disk did not appear")
    if sha256("/dev/vdi") != replacement_hash:
        raise RuntimeError("replacement provisioning disk was modified")
    serial("QEMU_UNPLUG_RECOVERY_OK")
    serial("QEMU_PROVISIONING_OK")


def begin_power_loss() -> None:
    operation = operation_id_from(provision("/dev/vdh", "qemu-power-loss", start_only=True))
    unit = f"btrfs-backup-device-preparation@{operation}.service"
    wait_for_helper_pid(unit)
    mapper = "/dev/mapper/btrfs-backup-qemu-power-loss"
    if not wait_until(lambda: device_exists(mapper), 1200, 0.025):
        raise RuntimeError("power-loss mapper did not appear")
    stop_helper(unit)
    POWER_LOSS_MARKER.write_text(f"{operation}\n")
    run(["sync", str(POWER_LOSS_MARKER)])
    serial("QEMU_POWER_LOSS_READY")
    while True:
        time.sleep(60)


def diagnostics() -> None:
    serial(f"QEMU_SETUP_FAILED status=1")
    with SERIAL.open("a") as stream:
        for arguments in (
            ["systemctl", "status", "--no-pager", "--full", "btrfs-backupd.service"],
            ["journalctl", "--no-pager", "-b", "-u", "btrfs-backupd.service", "-n", "100"],
            ["journalctl", "--no-pager", "-b", "-t", "btrfs-backup-device-preparation", "-n", "100"],
        ):
            subprocess.run(arguments, stdout=stream, stderr=subprocess.STDOUT, check=False, text=True)
        for path in sorted(TRANSACTIONS.glob("*.json")):
            print(f"--- {path} ---", file=stream)
            operation = path.stem
            subprocess.run([
                "systemctl", "show", f"btrfs-backup-device-preparation@{operation}.service",
                "-p", "DevicePolicy", "-p", "DeviceAllow", "-p", "FragmentPath", "-p", "DropInPaths",
            ], stdout=stream, stderr=subprocess.STDOUT, check=False, text=True)
            print(path.read_text(), file=stream)


def execute(config: dict[str, object]) -> None:
    target_uuid = config.get("target_uuid")
    target_device_unit = config.get("target_device_unit")
    replacement_hash = config.get("replacement_hash")
    if not all(isinstance(value, str) and value for value in
               (target_uuid, target_device_unit, replacement_hash)):
        raise ValueError("guest setup configuration contains invalid values")
    run(["tar", "--zstd", "-xpf", str(SETUP / "package.pkg.tar.zst"), "-C", "/"])
    if recover_power_loss():
        return
    configure_hotplug(target_uuid, target_device_unit)
    prepare_source()
    systemctl("start", "polkit.service", "btrfs-backupd.service")
    verify_initial_provisioning()
    verify_manager_recovery()
    verify_helper_recovery()
    verify_unplug(replacement_hash)
    begin_power_loss()


def main() -> int:
    parser = argparse.ArgumentParser(description="Provision and exercise the disposable QEMU guest.")
    parser.add_argument("--config", type=Path, required=True)
    arguments = parser.parse_args()
    sys.stderr = SERIAL.open("a")
    try:
        execute(json.loads(arguments.config.read_text()))
    except Exception:
        diagnostics()
        raise
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
