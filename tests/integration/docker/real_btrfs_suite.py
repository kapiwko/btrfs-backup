#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import os
from pathlib import Path
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time


SOURCE_MOUNT = Path("/mnt/bb-real-source")
TARGET_MOUNT = Path("/mnt/btrfs-backup/default")
STAGING_MOUNT = Path("/mnt/bb-real-target-staging")
PROFILE = Path("/etc/btrfs-backup/profiles/default/profile.json")
UDEV_RULE = Path("/etc/udev/rules.d/98-btrfs-backup-loop-test.rules")


def run(arguments: list[str], *, capture: bool = False, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, check=check, text=True, capture_output=capture)


def required_executable(environment_name: str) -> str:
    value = os.environ.get(environment_name)
    if value is None or not Path(value).is_file() or not os.access(value, os.X_OK):
        raise RuntimeError(f"missing integration executable: {environment_name}")
    return value


def replace_device_node(path: Path, mode: int, major: int, minor: int) -> None:
    try:
        status = path.stat()
        identity_matches = os.major(status.st_rdev) == major and os.minor(status.st_rdev) == minor
        if stat.S_IFMT(status.st_mode) == mode and identity_matches:
            return
    except FileNotFoundError:
        pass
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.unlink(missing_ok=True)
    os.mknod(temporary, mode | 0o600, os.makedev(major, minor))
    temporary.replace(path)


def replace_symlink(path: Path, target: str) -> None:
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.unlink(missing_ok=True)
    temporary.symlink_to(target)
    temporary.replace(path)


def materialize_devices() -> None:
    Path("/dev/block").mkdir(parents=True, exist_ok=True)
    Path("/dev/mapper").mkdir(parents=True, exist_ok=True)
    Path("/dev/disk/by-uuid").mkdir(parents=True, exist_ok=True)
    Path("/dev/disk/by-partuuid").mkdir(parents=True, exist_ok=True)
    for device in Path("/sys/class/block").glob("loop*"):
        dev = device / "dev"
        if not dev.is_file():
            continue
        major, minor = map(int, dev.read_text().strip().split(":"))
        name = device.name
        replace_device_node(Path("/dev") / name, stat.S_IFBLK, major, minor)
        replace_symlink(Path("/dev/block") / f"{major}:{minor}", f"../{name}")
        if "p" in name:
            for field, directory in (("UUID", "by-uuid"), ("PART_ENTRY_UUID", "by-partuuid")):
                result = run(["blkid", "-p", "-s", field, "-o", "value", f"/dev/{name}"], capture=True, check=False)
                value = result.stdout.strip()
                if result.returncode == 0 and value:
                    replace_symlink(Path("/dev/disk") / directory / value, f"../../{name}")
    for device in Path("/sys/class/block").glob("dm-*"):
        try:
            name = (device / "dm/name").read_text().strip()
            major, minor = map(int, (device / "dev").read_text().strip().split(":"))
        except FileNotFoundError:
            continue
        if not (name.startswith("btrfs-backup-") or name.startswith("bb-real-")):
            continue
        replace_device_node(Path("/dev") / device.name, stat.S_IFBLK, major, minor)
        replace_symlink(Path("/dev/mapper") / name, f"../{device.name}")
        replace_symlink(Path("/dev/block") / f"{major}:{minor}", f"../{device.name}")


def device_monitor() -> int:
    while True:
        try:
            materialize_devices()
        except (OSError, subprocess.SubprocessError):
            pass
        time.sleep(0.05)


class RealBtrfsSuite:
    def __init__(self) -> None:
        if os.geteuid() != 0 or os.environ.get("BTRFSBACKUP_REAL_BTRFS_CONTAINER") != "1":
            raise RuntimeError("real Btrfs suite requires root in its disposable container")
        self.root = Path(tempfile.mkdtemp(prefix="btrfs-backup-real.", dir="/tmp"))
        self.source_image = self.root / "source.img"
        self.target_image = self.root / "target.img"
        self.passphrase = self.root / "luks.pass"
        self.mapper_name = f"bb-real-target-{self.root.name.rsplit('.', 1)[-1]}"
        self.mapper_path = Path("/dev/mapper") / self.mapper_name
        self.source_loop: str | None = None
        self.target_loop: str | None = None
        self.monitor: subprocess.Popen[str] | None = None
        self.executables = {
            name: required_executable(environment)
            for name, environment in {
                "browse": "BTRFSBACKUP_BROWSE_SESSION_CLIENT",
                "provisioning": "BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT",
                "real": "BTRFSBACKUP_REAL_BTRFS_TESTS",
                "runtime": "BTRFSBACKUP_REAL_INSTALLED_RUNTIME_TESTS",
                "mapper": "BTRFSBACKUP_REAL_MAPPER_LIFECYCLE_TESTS",
                "hook": "BTRFSBACKUP_REAL_TRUSTED_HOOK_TESTS",
                "sandbox": "BTRFSBACKUP_REAL_SANDBOXED_SYSTEMD_TESTS",
                "dbus": "BTRFSBACKUP_REAL_SYSTEM_DBUS_BACKUP_TESTS",
                "independence": "BTRFSBACKUP_REAL_MANAGER_INDEPENDENCE_TESTS",
            }.items()
        }

    def close(self) -> None:
        if self.monitor is not None:
            self.monitor.terminate()
            try:
                self.monitor.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.monitor.kill()
                self.monitor.wait()
            self.monitor = None
        for mount in (TARGET_MOUNT, STAGING_MOUNT, SOURCE_MOUNT):
            run(["umount", "-R", str(mount)], capture=True, check=False)
        for _ in range(5):
            if run(["cryptsetup", "close", self.mapper_name], capture=True, check=False).returncode == 0:
                break
            time.sleep(0.1)
        for loop in (self.target_loop, self.source_loop):
            if loop is not None:
                run(["losetup", "-d", loop], capture=True, check=False)
        UDEV_RULE.unlink(missing_ok=True)
        for path in (SOURCE_MOUNT, TARGET_MOUNT, STAGING_MOUNT):
            shutil.rmtree(path, ignore_errors=True)
        shutil.rmtree(self.root, ignore_errors=True)

    def prepare_system(self) -> None:
        if not Path("/dev/loop-control").exists():
            os.mknod("/dev/loop-control", stat.S_IFCHR | 0o600, os.makedev(10, 237))
        for index in range(64):
            path = Path(f"/dev/loop{index}")
            if not path.exists():
                os.mknod(path, stat.S_IFBLK | 0o600, os.makedev(7, index))
        deadline = time.monotonic() + 10
        while run(["systemctl", "show-environment"], capture=True, check=False).returncode != 0:
            if time.monotonic() >= deadline:
                raise RuntimeError("systemd did not become ready before provisioning setup")
            time.sleep(0.1)
        run(["systemctl", "start", "systemd-udevd.service"])
        UDEV_RULE.write_text(
            'SUBSYSTEM=="block", KERNEL=="loop*", ENV{ID_BUS}="usb", '
            'ENV{ID_SERIAL}="btrfs-backup-test-$kernel"\n'
        )
        run(["udevadm", "control", "--reload"])
        self.monitor = subprocess.Popen([sys.executable, __file__, "--device-monitor"], text=True)
        SOURCE_MOUNT.mkdir(parents=True, exist_ok=True)
        TARGET_MOUNT.mkdir(parents=True, exist_ok=True)
        (self.root / "logs").mkdir(mode=0o700)

    def prepare_filesystems(self) -> None:
        self.passphrase.write_text("btrfs-backup-real-test-passphrase\n")
        self.passphrase.chmod(0o600)
        for image in (self.source_image, self.target_image):
            with image.open("wb") as output:
                output.truncate(768 * 1024 * 1024)
        self.source_loop = run(["losetup", "--find", "--show", str(self.source_image)], capture=True).stdout.strip()
        self.target_loop = run(["losetup", "--find", "--show", str(self.target_image)], capture=True).stdout.strip()
        run(["mkfs.btrfs", "-q", "-f", self.source_loop])
        run(["cryptsetup", "luksFormat", "--batch-mode", "--type", "luks2", "--pbkdf", "pbkdf2",
             "--pbkdf-force-iterations", "1000", "--key-file", str(self.passphrase), self.target_loop])
        run(["cryptsetup", "open", "--key-file", str(self.passphrase), self.target_loop, self.mapper_name])
        run(["udevadm", "settle", "--timeout=10"])
        run(["dmsetup", "mknodes", self.mapper_name])
        if not self.mapper_path.is_block_device():
            raise RuntimeError(f"cryptsetup mapper was not created: {self.mapper_path}")
        run(["mkfs.btrfs", "-q", "-f", str(self.mapper_path)])
        run(["mount", "-o", "noatime,compress=zstd:3", self.source_loop, str(SOURCE_MOUNT)])
        run(["btrfs", "subvolume", "create", str(SOURCE_MOUNT / "home")], capture=True)
        run(["mount", "--bind", str(SOURCE_MOUNT / "home"), str(SOURCE_MOUNT / "home")])
        (SOURCE_MOUNT / ".snapshots/home").mkdir(parents=True, mode=0o700)
        run(["mount", "-o", "noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd:3",
             str(self.mapper_path), str(TARGET_MOUNT)])
        (TARGET_MOUNT / "snapshots").mkdir(mode=0o700)
        (TARGET_MOUNT / ".incoming").mkdir(mode=0o700)
        (SOURCE_MOUNT / "home/file-a.txt").write_text("alpha\n")
        (SOURCE_MOUNT / "home/dir").mkdir(mode=0o755)
        (SOURCE_MOUNT / "home/dir/blob.bin").write_bytes(os.urandom(2 * 1024 * 1024))
        run(["sync", "-f", str(SOURCE_MOUNT)])

    def execute(self) -> None:
        package_dir = os.environ.get("BTRFSBACKUP_PACKAGE_DIR", "/packages")
        run([sys.executable, "/work/tests/integration/docker/arch_migration_transaction.py", package_dir])
        run([self.executables["real"], "/usr/bin/btrfs-backupctl", self.executables["browse"],
             self.executables["provisioning"], package_dir, "/work"])
        self.prepare_filesystems()
        common_mapper = [str(TARGET_MOUNT), self.target_loop or "", self.mapper_name, str(self.passphrase)]
        run([self.executables["mapper"], *common_mapper])
        run([self.executables["runtime"], "/usr/bin/btrfs-backupctl", "/usr/bin/btrfs-backup",
             str(self.root), str(SOURCE_MOUNT), str(TARGET_MOUNT), self.target_loop or "",
             self.mapper_name, str(self.passphrase)])
        run([self.executables["dbus"], str(SOURCE_MOUNT), str(TARGET_MOUNT), str(self.root)])
        run([self.executables["independence"], "/usr/bin/btrfs-backup", str(PROFILE), str(self.root),
             str(SOURCE_MOUNT), str(TARGET_MOUNT)])
        run([self.executables["hook"], "/usr/bin/btrfs-backup", str(PROFILE), str(self.root)])
        run([self.executables["mapper"], *common_mapper])
        run([self.executables["sandbox"], str(SOURCE_MOUNT), str(TARGET_MOUNT), str(STAGING_MOUNT),
             self.mapper_name, str(PROFILE)])
        print(f"Real Btrfs integration test completed in {self.root}", flush=True)

    def run(self) -> None:
        try:
            self.prepare_system()
            self.execute()
        finally:
            self.close()


def main() -> int:
    if sys.argv[1:] == ["--device-monitor"]:
        return device_monitor()
    RealBtrfsSuite().run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
