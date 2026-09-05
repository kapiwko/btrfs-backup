#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import fcntl
import os
from pathlib import Path
import random
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

sys_path = str(Path(__file__).resolve().parents[1] / "qemu")
sys.path.insert(0, sys_path)
from qmp_client import QmpClient  # noqa: E402


ROOTFS_CACHE_VERSION = "manual-v2"
SETUP_UNIT = """[Unit]
Description=Prepare the interactive btrfs-backup laboratory
After=systemd-udevd.service dev-vdb.device
Requires=dev-vdb.device
Before=display-manager.service

[Service]
Type=oneshot
ExecStart=/usr/bin/mkdir -p /run/btrfs-backup-manual-setup
ExecStart=/usr/bin/mount -o ro /dev/vdb /run/btrfs-backup-manual-setup
ExecStart=/usr/bin/python3 /run/btrfs-backup-manual-setup/manual_guest.py
RemainAfterExit=yes

[Install]
WantedBy=graphical.target
"""


def command(arguments: list[str], *, capture: bool = False, quiet: bool = False) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(arguments, check=False, text=True, capture_output=capture or quiet,
                            env={**os.environ, "LC_ALL": "C"})
    if result.returncode != 0:
        if quiet:
            if result.stdout:
                print(result.stdout, file=sys.stderr, end="")
            if result.stderr:
                print(result.stderr, file=sys.stderr, end="")
        result.check_returncode()
    return result


class ManualVm:
    def __init__(self) -> None:
        if os.geteuid() != 0:
            raise RuntimeError("the QEMU container must run as root")
        artifact_dir = os.environ.get("MANUAL_ARTIFACT_DIR")
        self.persistent_work = artifact_dir is not None
        self.work = Path(artifact_dir) if artifact_dir else Path(tempfile.mkdtemp(prefix="btrfs-backup-manual.", dir="/tmp"))
        self.work.mkdir(parents=True, exist_ok=True)
        self.cache = Path(os.environ.get("QEMU_ROOTFS_CACHE_DIR", "/qemu-cache"))
        key = os.environ.get("QEMU_ROOTFS_CACHE_KEY", "local")
        self.root_image = self.cache / f"rootfs-{ROOTFS_CACHE_VERSION}-{key}.img"
        self.mount = self.work / "mount"
        self.setup = self.work / "setup.img"
        self.source = self.work / "source.img"
        self.target = self.work / "target.img"
        self.blank = self.work / "blank.img"
        self.partitioned = self.work / "partitioned.img"
        self.incompatible = self.work / "incompatible.img"
        self.adopt = self.work / "adopt.img"
        self.console = self.work / "console.log"
        self.qmp_socket = self.work / "qmp.sock"
        self.qemu: subprocess.Popen[bytes] | None = None
        self.target_connected = True
        self.kernel_release, self.kernel = self._kernel()

    @staticmethod
    def _kernel() -> tuple[str, Path]:
        release = os.uname().release
        image = Path("/usr/lib/modules") / release / "vmlinuz"
        if not image.is_file():
            image = Path("/boot/vmlinuz-linux")
            release = sorted(Path("/usr/lib/modules").iterdir())[-1].name
        return release, image

    def close(self) -> None:
        for process in (self.qemu,):
            if process is not None and process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
        if not self.persistent_work:
            shutil.rmtree(self.work, ignore_errors=True)

    def prepare_root(self) -> None:
        self.cache.mkdir(parents=True, exist_ok=True)
        self.mount.mkdir(exist_ok=True)
        lock_path = self.cache / f"{self.root_image.name}.lock"
        with lock_path.open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if self.root_image.exists():
                return
            temporary = self.root_image.with_suffix(".tmp")
            temporary.unlink(missing_ok=True)
            command(["truncate", "-s", "16G", str(temporary)])
            command(["mkfs.ext4", "-q", "-F", str(temporary)])
            command(["mount", "-o", "loop", str(temporary), str(self.mount)])
            try:
                command(["tar", "-xpf", os.environ["QEMU_ROOTFS_TAR"], "-C", str(self.mount)])
                modules = self.mount / "usr/lib/modules"
                modules.mkdir(parents=True, exist_ok=True)
                guest_modules = modules / self.kernel_release
                if not guest_modules.exists():
                    shutil.copytree(Path("/usr/lib/modules") / self.kernel_release,
                                    guest_modules, symlinks=True)
                (self.mount / ".dockerenv").unlink(missing_ok=True)
                (self.mount / "run/systemd/container").unlink(missing_ok=True)
                write = lambda path, value: (self.mount / path).write_text(value)
                write("etc/hostname", "btrfs-backup-lab\n")
                write("etc/machine-id", "")
                unit = self.mount / "etc/systemd/system/btrfs-backup-manual-setup.service"
                unit.write_text(SETUP_UNIT)
                wants = self.mount / "etc/systemd/system/graphical.target.wants"
                wants.mkdir(parents=True, exist_ok=True)
                (wants / unit.name).symlink_to("../btrfs-backup-manual-setup.service")
                display_manager = self.mount / "etc/systemd/system/display-manager.service"
                display_manager.unlink(missing_ok=True)
                display_manager.symlink_to("/usr/lib/systemd/system/sddm.service")
                (wants / "display-manager.service").symlink_to("../display-manager.service")
                default = self.mount / "etc/systemd/system/default.target"
                default.unlink(missing_ok=True)
                default.symlink_to("/usr/lib/systemd/system/graphical.target")
            finally:
                command(["umount", str(self.mount)])
            temporary.replace(self.root_image)

    def prepare_disks(self) -> None:
        command(["truncate", "-s", "256M", str(self.setup)])
        command(["mkfs.ext4", "-q", "-F", str(self.setup)])
        command(["mount", "-o", "loop", str(self.setup), str(self.mount)])
        try:
            packages = sorted(Path(os.environ["QEMU_PACKAGE_DIR"]).glob("*.pkg.tar.zst"))
            base = [path for path in packages if "btrfs-backup-kde-" not in path.name]
            kde = [path for path in packages if "btrfs-backup-kde-" in path.name]
            if len(base) != 1 or len(kde) != 1:
                raise RuntimeError("expected exactly one base package and one KDE package")
            shutil.copy2(base[0], self.mount / base[0].name)
            shutil.copy2(kde[0], self.mount / kde[0].name)
            shutil.copy2(Path(__file__).with_name("manual_guest.py"), self.mount / "manual_guest.py")
            shutil.copy2(Path(__file__).with_name("manual_change.py"), self.mount / "manual_change.py")
        finally:
            command(["umount", str(self.mount)])
        command(["truncate", "-s", "4G", str(self.source)])
        command(["mkfs.btrfs", "-q", "-f", "-L", "MANUAL-SOURCE", str(self.source)])
        command(["truncate", "-s", "10G", str(self.target)])
        command(["truncate", "-s", "2G", str(self.blank)])
        command(["truncate", "-s", "3G", str(self.partitioned)])
        subprocess.run(["sfdisk", "--quiet", str(self.partitioned)], check=True, text=True,
                       input="label: gpt\nsize=512M,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\n")
        command(["truncate", "-s", "1G", str(self.incompatible)])
        command(["mkfs.ext4", "-q", "-F", "-L", "INCOMPATIBLE", str(self.incompatible)])
        command(["truncate", "-s", "3G", str(self.adopt)])
        command(["mount", "-o", "loop", str(self.setup), str(self.mount)])
        try:
            self.prepare_backup_fixture(packages)
        finally:
            command(["umount", str(self.mount)])

    @staticmethod
    def write_data(path: Path, size_mib: int, seed: int) -> None:
        generator = random.Random(seed)
        with path.open("wb") as stream:
            for _ in range(size_mib):
                stream.write(generator.randbytes(1024 * 1024))

    def prepare_backup_fixture(self, packages: list[Path]) -> None:
        command(["pacman", "-U", "--noconfirm", *map(str, packages)], quiet=True)
        source_root = Path("/srv/manual-source")
        target_root = Path("/mnt/btrfs-backup/manual-lab")
        source_root.mkdir(parents=True, exist_ok=True)
        target_root.mkdir(parents=True, exist_ok=True)
        key = self.work / "manual-target.key"
        key.write_text("manual-backup-key\n")
        key.chmod(0o600)
        active_key = Path("/root/.config/btrfs-backup/manual-target.key")
        active_key.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(key, active_key)
        mapper = "manual-backup"
        adopt_mapper = "manual-fixture-adopt"
        command(["mount", "-o", "loop", str(self.source), str(source_root)])
        try:
            for relative in ("home", "www"):
                command(["btrfs", "subvolume", "create", str(source_root / relative)], quiet=True)
            (source_root / ".snapshots/home").mkdir(parents=True)
            (source_root / ".snapshots/www").mkdir(parents=True)
            home = source_root / "home/tester"
            (home / "Documents").mkdir(parents=True)
            (home / "Pictures").mkdir()
            (home / "Documents/report.txt").write_text("First report version for restore testing.\n")
            (home / "Documents/_select_es_numer_skladowej_as_numer_es_nazwa_skladowej_as_nazwa__202507011505.csv").write_text(
                "number;name\n32;Example component\n")
            (home / "Pictures/holidays.txt").write_text("A placeholder picture file for UI testing.\n")
            (home / ".hidden-file").write_text("Hidden backup contents.\n")
            self.write_data(home / "Large cancellation file.bin", 384, 20260905)
            for path in [home, *home.rglob("*")]:
                os.chown(path, 1000, 1000)
            www = source_root / "www"
            (www / "config").mkdir(parents=True)
            (www / "private").mkdir()
            (www / "index.html").write_text("<h1>Production version 1</h1>\n")
            (www / "config/readable.conf").write_text("public_value=manual-test\n")
            (www / "private/secret.conf").write_text("secret_value=backup-only\n")
            (www / "config/readable.conf").chmod(0o640)
            (www / "private/secret.conf").chmod(0o600)
            os.chown(www / "config/readable.conf", 0, 1000)

            command(["cryptsetup", "luksFormat", "--type", "luks2", "--pbkdf", "pbkdf2",
                     "--pbkdf-force-iterations", "1000", "--batch-mode", "--key-file", str(key), str(self.target)])
            command(["cryptsetup", "open", "--key-file", str(key), str(self.target), mapper])
            try:
                command(["mkfs.btrfs", "-q", "-f", "-L", "BTRFS-BACKUP-MANUAL", f"/dev/mapper/{mapper}"])
                command(["mount", "-o", "noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd",
                         f"/dev/mapper/{mapper}", str(target_root)])
                try:
                    luks_uuid = command(["cryptsetup", "luksUUID", str(self.target)], capture=True).stdout.strip()
                    btrfs_uuid = command(["blkid", "-s", "UUID", "-o", "value", f"/dev/mapper/{mapper}"], capture=True).stdout.strip()
                    profile = self.work / "profile-input.json"
                    command(["btrfs-backupctl", "profile", "create", "--output", str(profile),
                             "--profile", "manual-lab", "--name", "Manual test backup",
                             "--device", f"/dev/disk/by-uuid/{luks_uuid}", "--luks-uuid", luks_uuid,
                             "--btrfs-uuid", btrfs_uuid, "--serial", "bb-manual-target",
                             "--mapper-name", "manual-backup", "--keyfile", "/root/.config/btrfs-backup/manual-target.key",
                             "--daily-limit", "false", "--incremental-required", "true", "--auto-eject", "false",
                             "--minimum-target-free-bytes", "0", "--minimum-local-free-bytes", "0",
                             "--source", "home", "Home files", str(source_root / "home"),
                             str(source_root / ".snapshots/home"), "home", "10", "10",
                             "--source", "www", "Web server", str(source_root / "www"),
                             str(source_root / ".snapshots/www"), "www", "10", "10"])
                    rendered = self.work / "rendered"
                    command(["btrfs-backupctl", "profile", "--etc-root", str(rendered / "config"),
                             "--udev-root", str(rendered / "udev"), "--systemd-root", str(rendered / "systemd"),
                             "--public-root", str(rendered / "public"), "save", "--file", str(profile)])
                    saved = rendered / "config/profiles/manual-lab/profile.json"
                    active = Path("/etc/btrfs-backup/profiles/manual-lab/profile.json")
                    active.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(saved, active)
                    command(["btrfs-backupctl", "runner", "execute", "--profile", "manual-lab", "--force"], quiet=True)
                    (home / "Documents/report.txt").write_text("Second report version for restore testing.\n")
                    (home / "Documents/new-file.txt").write_text("This file exists only in the newer version.\n")
                    (www / "index.html").write_text("<h1>Production version 2</h1>\n")
                    command(["btrfs-backupctl", "runner", "execute", "--profile", "manual-lab", "--force"], quiet=True)
                    shutil.copy2(saved, self.mount / "profile.json")
                    shutil.copy2(key, self.mount / "manual-target.key")
                finally:
                    command(["umount", str(target_root)])
            finally:
                command(["cryptsetup", "close", mapper])

            adopt_key = self.work / "manual-adopt.key"
            adopt_key.write_text("manual-adopt-key\n")
            adopt_key.chmod(0o600)
            command(["cryptsetup", "luksFormat", "--type", "luks2", "--pbkdf", "pbkdf2",
                     "--pbkdf-force-iterations", "1000", "--batch-mode", "--key-file", str(adopt_key), str(self.adopt)])
            command(["cryptsetup", "open", "--key-file", str(adopt_key), str(self.adopt), adopt_mapper])
            try:
                command(["mkfs.btrfs", "-q", "-f", "-L", "ADOPT-ME", f"/dev/mapper/{adopt_mapper}"])
            finally:
                command(["cryptsetup", "close", adopt_mapper])
        finally:
            command(["umount", str(source_root)])

    def export_artifacts(self) -> None:
        shutil.copy2(self.kernel, self.work / "vmlinuz")
        host_cache = Path(os.environ.get("MANUAL_HOST_CACHE_DIR", str(self.cache)))
        host_artifacts = Path(os.environ.get("MANUAL_HOST_ARTIFACT_DIR", str(self.work)))
        disks = (("root", self.root_image, "16G"), ("source", self.source, "4G"),
                 ("target", self.target, "10G"), ("blank", self.blank, "2G"),
                 ("partitioned", self.partitioned, "3G"),
                 ("incompatible", self.incompatible, "1G"), ("adopt", self.adopt, "3G"))
        for name, backing, size in disks:
            overlay = self.work / f"{name}.qcow2"
            overlay.unlink(missing_ok=True)
            host_backing = host_cache / backing.name if name == "root" else host_artifacts / backing.name
            command(["qemu-img", "create", "-q", "-u", "-f", "qcow2", "-F", "raw",
                     "-b", str(host_backing), str(overlay), size])
        (self.work / "console.log").touch()
        uid = int(os.environ.get("MANUAL_HOST_UID", "0"))
        gid = int(os.environ.get("MANUAL_HOST_GID", "0"))
        for path in self.work.iterdir():
            os.chown(path, uid, gid)
            path.chmod(0o666 if path.name == "console.log" else 0o664)

    def start(self) -> None:
        args = [
            "qemu-system-x86_64", "-machine", "q35", "-m", "3072", "-smp", "4",
            "-kernel", str(self.kernel), "-append",
            "root=/dev/vda rw rootfstype=ext4 quiet console=ttyS0 systemd.unit=graphical.target",
            "-drive", f"file={self.root_image},if=none,id=root,format=raw,snapshot=on",
            "-device", "virtio-blk-pci,drive=root,serial=bb-manual-root",
            "-drive", f"file={self.setup},if=none,id=setup,format=raw,snapshot=on",
            "-device", "virtio-blk-pci,drive=setup,serial=bb-manual-setup",
            "-drive", f"file={self.source},if=none,id=source,format=raw,snapshot=on",
            "-device", "virtio-blk-pci,drive=source,serial=bb-manual-source",
            "-blockdev", f"driver=file,filename={self.target},node-name=target-file",
            "-blockdev", "driver=raw,file=target-file,node-name=target",
            "-device", "pcie-root-port,id=target-port,slot=0x10,chassis=10",
            "-device", "virtio-blk-pci,drive=target,id=target-device,serial=bb-manual-target,bus=target-port",
            "-device", "virtio-vga", "-device", "qemu-xhci", "-device", "usb-tablet",
            "-display", "none", "-vnc", "0.0.0.0:0", "-qmp", f"unix:{self.qmp_socket},server=on,wait=off",
            "-serial", f"file:{self.console}", "-monitor", "none", "-nic", "none",
        ]
        acceleration = ["-enable-kvm", "-cpu", "host"] if os.access("/dev/kvm", os.R_OK | os.W_OK) else ["-accel", "tcg", "-cpu", "max"]
        self.qemu = subprocess.Popen([args[0], *acceleration, *args[1:]])

    def wait_qmp(self) -> None:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if self.qmp_socket.exists():
                try:
                    QmpClient(self.qmp_socket).execute("query-status")
                    return
                except (ConnectionError, OSError, RuntimeError):
                    pass
            time.sleep(0.2)
        raise RuntimeError("QEMU monitor did not become ready")

    def watch_ready(self) -> None:
        seen = False
        while self.qemu is not None and self.qemu.poll() is None:
            if not seen and self.console.exists() and "MANUAL_LAB_READY" in self.console.read_text(errors="replace"):
                print("\nThe laboratory is ready. Plasma will show the desktop shortly.\n", flush=True)
                seen = True
            time.sleep(0.5)

    def control(self) -> None:
        self.wait_qmp()
        threading.Thread(target=self.watch_ready, daemon=True).start()
        print("Connect a VNC client to 127.0.0.1:5900.")
        print("Commands: connect, disconnect, status, quit")
        while self.qemu is not None and self.qemu.poll() is None:
            try:
                action = input("manual-lab> ").strip().lower()
            except EOFError:
                self.qemu.wait()
                return
            client = QmpClient(self.qmp_socket)
            if action == "disconnect":
                if self.target_connected:
                    client.execute("device_del", {"id": "target-device"})
                    self.target_connected = False
                    print("The backup disk was physically disconnected.")
                else:
                    print("The backup disk is already disconnected.")
            elif action == "connect":
                if not self.target_connected:
                    client.execute("device_add", {"driver": "virtio-blk-pci", "drive": "target",
                                                   "id": "target-device", "serial": "bb-manual-target",
                                                   "bus": "target-port"})
                    self.target_connected = True
                    print("The backup disk was connected.")
                else:
                    print("The backup disk is already connected.")
            elif action == "status":
                client.execute("query-status")
                print(f"VM: running; disk: {'connected' if self.target_connected else 'disconnected'}")
            elif action in ("quit", "exit"):
                client.execute("system_powerdown")
                try:
                    self.qemu.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    self.qemu.terminate()
                return
            elif action:
                print("Unknown command. Available: connect, disconnect, status, quit")

    def run(self) -> None:
        try:
            self.prepare_root()
            self.prepare_disks()
            self.start()
            self.control()
        finally:
            self.close()


if __name__ == "__main__":
    vm = ManualVm()
    if "--prepare-only" in sys.argv:
        try:
            vm.prepare_root()
            vm.prepare_disks()
            vm.export_artifacts()
        finally:
            vm.close()
    else:
        vm.run()
