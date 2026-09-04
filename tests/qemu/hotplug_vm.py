#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import fcntl
import hashlib
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time

from hotplug_guest_payload import render_guest_setup
from qmp_client import QmpClient


ROOT = Path(__file__).resolve().parents[2]
SETUP_UNIT = """[Unit]
Description=Install the QEMU test payload
After=systemd-udevd.service dev-vdb.device
Requires=dev-vdb.device

[Service]
Type=oneshot
ExecStart=/usr/bin/mkdir -p /run/qemu-test-setup
ExecStart=/usr/bin/mount -o ro /dev/vdb /run/qemu-test-setup
ExecStart=/usr/bin/sh /run/qemu-test-setup/setup.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
"""


def command(arguments: list[str], *, capture: bool = False, quiet: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        arguments,
        check=True,
        text=True,
        capture_output=capture,
        stdout=subprocess.DEVNULL if quiet else None,
        env={**os.environ, "LC_ALL": "C"},
    )


class HotplugVm:
    def __init__(self) -> None:
        if os.geteuid() != 0:
            raise RuntimeError("QEMU inner runner requires root")
        self.root = Path(tempfile.mkdtemp(prefix="btrfs-backup-qemu.", dir="/tmp"))
        self.cache = Path(os.environ.get("QEMU_ROOTFS_CACHE_DIR", "/tmp/btrfs-backup-qemu-cache"))
        cache_key = os.environ.get("QEMU_ROOTFS_CACHE_KEY", "local")
        self.root_image = self.cache / f"rootfs-v2-{cache_key}.img"
        self.root_image_temporary: Path | None = None
        self.root_mount = self.root / "root"
        self.console_log = self.root / "console.log"
        self.qmp_socket = self.root / "qmp.sock"
        self.qemu: subprocess.Popen[bytes] | None = None
        self.root_mounted = False
        self.images = {name: self.root / f"{name}.img" for name in (
            "setup", "target", "source", "whole-device", "partition-device",
            "manager-kill", "helper-kill", "power-loss", "unplug", "replacement",
        )}
        self.client = Path(os.environ["BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT"])
        self.package_dir = Path(os.environ["QEMU_PACKAGE_DIR"])
        self.kernel_release, self.kernel_image = self._kernel()

    @staticmethod
    def _kernel() -> tuple[str, Path]:
        release = os.uname().release
        image = Path("/usr/lib/modules") / release / "vmlinuz"
        if not os.access(image, os.R_OK) and os.access("/boot/vmlinuz-linux", os.R_OK):
            image = Path("/boot/vmlinuz-linux")
            releases = sorted(Path("/usr/lib/modules").iterdir(), key=lambda path: path.name)
            release = releases[-1].name
        return release, image

    def close(self) -> None:
        if self.qemu is not None and self.qemu.poll() is None:
            self.qemu.send_signal(signal.SIGTERM)
            try:
                self.qemu.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.qemu.kill()
                self.qemu.wait()
        if self.root_mounted:
            subprocess.run(["umount", str(self.root_mount)], check=False)
        if self.root_image_temporary is not None:
            self.root_image_temporary.unlink(missing_ok=True)
        if os.environ.get("QEMU_KEEP_ARTIFACTS"):
            print(f"QEMU artifacts: {self.root}")
        else:
            shutil.rmtree(self.root, ignore_errors=True)

    def validate(self) -> None:
        required = ("cryptsetup", "flock", "mkfs.btrfs", "mkfs.ext4", "qemu-system-x86_64", "sfdisk", "tar")
        missing = [name for name in required if shutil.which(name) is None]
        if missing:
            raise RuntimeError(f"missing commands: {', '.join(missing)}")
        if not os.access(self.kernel_image, os.R_OK):
            raise RuntimeError(f"host kernel image is not readable: {self.kernel_image}")
        if not (Path("/usr/lib/modules") / self.kernel_release).is_dir():
            raise RuntimeError(f"host kernel modules are unavailable: {self.kernel_release}")

    def prepare_rootfs(self) -> None:
        self.cache.mkdir(parents=True, exist_ok=True)
        self.root_mount.mkdir(mode=0o755)
        lock_path = self.cache / f"{self.root_image.name}.lock"
        with lock_path.open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if self.root_image.exists():
                return
            descriptor, temporary = tempfile.mkstemp(prefix=".rootfs.", suffix=".img", dir=self.cache)
            os.close(descriptor)
            self.root_image_temporary = Path(temporary)
            command(["truncate", "-s", "6G", temporary])
            command(["mkfs.ext4", "-q", "-F", temporary])
            self._mount(self.root_image_temporary)
            rootfs_tar = os.environ.get("QEMU_ROOTFS_TAR")
            if not rootfs_tar:
                raise RuntimeError("QEMU_ROOTFS_TAR is required by the Python runner")
            command(["tar", "-xpf", rootfs_tar, "-C", str(self.root_mount)])
            modules = self.root_mount / "usr/lib/modules"
            wants = self.root_mount / "etc/systemd/system/multi-user.target.wants"
            modules.mkdir(parents=True, exist_ok=True)
            wants.mkdir(parents=True, exist_ok=True)
            shutil.copytree(Path("/usr/lib/modules") / self.kernel_release, modules / self.kernel_release, symlinks=True)
            (self.root_mount / ".dockerenv").unlink(missing_ok=True)
            (self.root_mount / "run/systemd/container").unlink(missing_ok=True)
            (self.root_mount / "etc/hostname").write_text("btrfs-backup-qemu\n")
            (self.root_mount / "etc/machine-id").write_text("")
            unit = self.root_mount / "etc/systemd/system/qemu-test-setup.service"
            unit.write_text(SETUP_UNIT)
            (wants / "qemu-test-setup.service").symlink_to("../qemu-test-setup.service")
            default = self.root_mount / "etc/systemd/system/default.target"
            default.unlink(missing_ok=True)
            default.symlink_to("/usr/lib/systemd/system/multi-user.target")
            self._unmount()
            self.root_image_temporary.replace(self.root_image)
            self.root_image_temporary = None

    def _mount(self, image: Path) -> None:
        command(["mount", "-o", "loop", str(image), str(self.root_mount)])
        self.root_mounted = True

    def _unmount(self) -> None:
        command(["umount", str(self.root_mount)])
        self.root_mounted = False

    def prepare_disks(self) -> None:
        setup = self.images["setup"]
        command(["truncate", "-s", "128M", str(setup)])
        command(["mkfs.ext4", "-q", "-F", str(setup)])
        self._mount(setup)
        packages = sorted(self.package_dir.glob("btrfs-backup-[0-9]*.pkg.tar.zst"))
        if not packages:
            raise RuntimeError(f"no base Arch package found in {self.package_dir}")
        shutil.copy2(packages[0], self.root_mount / "package.pkg.tar.zst")
        shutil.copy2(self.client, self.root_mount / "device-provisioning-client")

        key = self.root / "luks.key"
        key.write_text("qemu-hotplug-test-key\n")
        key.chmod(0o600)
        shutil.copy2(key, self.root_mount / "provisioning.key")
        target = self.images["target"]
        command(["truncate", "-s", "64M", str(target)])
        command(["cryptsetup", "luksFormat", "--type", "luks2", "--pbkdf", "pbkdf2",
                 "--pbkdf-force-iterations", "1000", "--batch-mode", "--key-file", str(key), str(target)])
        target_uuid = command(["cryptsetup", "luksUUID", str(target)], capture=True).stdout.strip()
        target_unit = command(["systemd-escape", "--path", "--suffix=device",
                               f"/dev/disk/by-uuid/{target_uuid}"], capture=True).stdout.strip()

        self._format_image("source", "384M", ["mkfs.btrfs", "-q", "-f"])
        self._format_image("whole-device", "512M", ["mkfs.ext4", "-q", "-F", "-L", "QEMU-WHOLE"])
        command(["truncate", "-s", "768M", str(self.images["partition-device"])])
        subprocess.run(["sfdisk", "--quiet", str(self.images["partition-device"])], check=True, text=True,
                       input="label: gpt\nsize=128M,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\n"
                             "size=512M,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\n")
        for name, size, label in (
            ("manager-kill", "544M", "QEMU-MANAGER-KILL"),
            ("helper-kill", "576M", "QEMU-HELPER-KILL"),
            ("power-loss", "608M", "QEMU-POWER-LOSS"),
            ("unplug", "640M", "QEMU-UNPLUG"),
            ("replacement", "672M", "QEMU-REPLACEMENT"),
        ):
            self._format_image(name, size, ["mkfs.ext4", "-q", "-F", "-L", label])
        digest = hashlib.sha256()
        with self.images["replacement"].open("rb") as replacement:
            while chunk := replacement.read(1024 * 1024):
                digest.update(chunk)
        replacement_hash = digest.hexdigest()
        payload = render_guest_setup(target_uuid, target_unit, replacement_hash)
        guest_setup = self.root_mount / "setup.sh"
        guest_setup.write_text(payload)
        guest_setup.chmod(0o755)
        self._unmount()

    def _format_image(self, name: str, size: str, formatter: list[str]) -> None:
        image = self.images[name]
        command(["truncate", "-s", size, str(image)])
        command([*formatter, str(image)])

    def start(self) -> None:
        drive = lambda name, disk_id, snapshot=True: [
            "-drive", f"file={self.images[name]},if=none,id={disk_id},format=raw" + (",snapshot=on" if snapshot else "")]
        args = ["qemu-system-x86_64", "-machine", "q35", "-m", "768", "-smp", "2", "-nographic",
                "-kernel", str(self.kernel_image), "-append",
                "root=/dev/vda rw rootfstype=ext4 console=ttyS0 systemd.unit=multi-user.target",
                "-drive", f"file={self.root_image},if=none,id=root-disk,format=raw,snapshot=on",
                "-device", "virtio-blk-pci,drive=root-disk,serial=bb-root",
                *drive("setup", "setup-disk", False), "-device", "virtio-blk-pci,drive=setup-disk,serial=bb-setup",
                *drive("source", "source-disk"), "-device", "virtio-blk-pci,drive=source-disk,serial=bb-source",
                *drive("whole-device", "whole-disk"), "-device", "virtio-blk-pci,drive=whole-disk,serial=bb-whole",
                *drive("partition-device", "partition-disk"), "-device", "virtio-blk-pci,drive=partition-disk,serial=bb-partition",
                *drive("manager-kill", "manager-kill-disk"), "-device", "virtio-blk-pci,drive=manager-kill-disk,serial=bb-manager-kill",
                *drive("helper-kill", "helper-kill-disk"), "-device", "virtio-blk-pci,drive=helper-kill-disk,serial=bb-helper-kill",
                *drive("power-loss", "power-loss-disk"), "-device", "virtio-blk-pci,drive=power-loss-disk,serial=bb-power-loss",
                "-device", "pcie-root-port,id=provisioning-port,slot=0x10,chassis=10", "-device", "qemu-xhci,id=xhci"]
        for name, node in (("unplug", "unplug"), ("replacement", "replacement"), ("target", "hotplug-target")):
            args += ["-blockdev", f"driver=file,filename={self.images[name]},node-name={node}-file",
                     "-blockdev", f"driver=raw,file={node}-file,node-name={node}"]
        args += ["-qmp", f"unix:{self.qmp_socket},server=on,wait=off", "-serial", f"file:{self.console_log}",
                 "-monitor", "none", "-nic", "none"]
        acceleration = ["-enable-kvm", "-cpu", "host"] if os.access("/dev/kvm", os.R_OK | os.W_OK) else ["-accel", "tcg", "-cpu", "max"]
        self.qemu = subprocess.Popen([args[0], *acceleration, *args[1:]])

    def wait_for(self, marker: str, failure: str, timeout: float = 300.0) -> None:
        assert self.qemu is not None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.console_log.exists() and marker in self.console_log.read_text(errors="replace"):
                return
            if self.qemu.poll() is not None:
                raise RuntimeError(failure)
            time.sleep(0.25)
        raise RuntimeError(failure)

    def qmp(self, name: str, arguments: dict[str, object] | None = None) -> None:
        QmpClient(self.qmp_socket).execute(name, arguments)

    def scenario(self) -> None:
        self.wait_for("QEMU_UNPLUG_ATTACH_READY", "guest did not request the provisioning hotplug disk")
        self.qmp("device_add", {"driver": "virtio-blk-pci", "drive": "unplug", "id": "provisioning-hotplug",
                                "serial": "qemu-unplug-original", "bus": "provisioning-port"})
        self.wait_for("QEMU_UNPLUG_READY", "guest did not reach the device-unplug boundary")
        self.qmp("device_del", {"id": "provisioning-hotplug"})
        self.wait_for("QEMU_REPLACEMENT_ATTACH_READY", "guest did not reject the unplugged provisioning target")
        self.qmp("device_add", {"driver": "virtio-blk-pci", "drive": "replacement", "id": "provisioning-hotplug",
                                "serial": "qemu-unplug-replacement", "bus": "provisioning-port"})
        self.wait_for("QEMU_UNPLUG_RECOVERY_OK", "guest modified or accepted the replacement provisioning device")
        self.wait_for("QEMU_POWER_LOSS_READY", "guest did not reach the power-loss boundary")
        self.qmp("system_reset")
        self.wait_for("QEMU_POWER_LOSS_RECOVERED", "guest did not recover the interrupted transaction after reset")
        self.wait_for("QEMU_READY", "QEMU guest did not become ready after recovery")
        self.require("QEMU_PROVISIONING_OK", "whole-device and existing-partition provisioning did not pass in QEMU")
        self.require("QEMU_MANAGER_KILL_OK", "manager SIGKILL recovery did not pass in QEMU")
        self.require("QEMU_HELPER_KILL_OK", "helper SIGKILL recovery did not pass in QEMU")
        self.qmp("device_add", {"driver": "usb-storage", "drive": "hotplug-target", "id": "target-usb"})
        self.wait_for("QEMU_HOTPLUG_OK_1", "udev did not start btrfs-backup@default.service after USB attachment", 15)
        self.qmp("device_del", {"id": "target-usb"})
        self.wait_for("QEMU_TARGET_STOP_1", "target activation remained active after USB removal", 15)
        self.qmp("device_add", {"driver": "usb-storage", "drive": "hotplug-target", "id": "target-usb"})
        self.wait_for("QEMU_HOTPLUG_OK_2", "udev did not restart btrfs-backup@default.service after USB reattachment", 15)
        self.require("QEMU_TARGET_START_2", "target activation did not restart after USB reattachment")

    def require(self, marker: str, failure: str) -> None:
        if marker not in self.console_log.read_text(errors="replace"):
            raise RuntimeError(failure)

    def run(self) -> None:
        try:
            self.validate()
            self.prepare_rootfs()
            self.prepare_disks()
            self.start()
            self.scenario()
            print("ok - QEMU provisioning, interruption recovery and USB hotplug pass in a system guest")
        except Exception:
            if self.console_log.exists():
                print("".join(self.console_log.read_text(errors="replace").splitlines(keepends=True)[-200:]), file=os.sys.stderr)
            raise
        finally:
            self.close()
