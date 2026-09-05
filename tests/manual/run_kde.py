#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[2]
DOMAIN = "btrfs-backup-manual-lab"
IMAGE = "btrfs-backup-manual-test:local"


def run(arguments: list[str], *, capture: bool = False, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, check=check, text=True, capture_output=capture,
                          env={**os.environ, "LC_ALL": "C"})


def image_id(image: str) -> str:
    return run(["docker", "image", "inspect", "--format", "{{.Id}}", image], capture=True).stdout.strip().removeprefix("sha256:")


def select_connection(configured: str | None) -> str:
    candidates = [configured] if configured else [os.environ.get("LIBVIRT_DEFAULT_URI"), "qemu:///system", "qemu:///session"]
    for candidate in dict.fromkeys(item for item in candidates if item):
        if run(["virsh", "-c", candidate, "uri"], capture=True, check=False).returncode == 0:
            return candidate
    raise RuntimeError("no working libvirt QEMU connection was found")


class ManualLab:
    def __init__(self, connection: str, *, viewer: bool) -> None:
        self.connection = connection
        self.viewer = viewer
        self.cache = Path(os.environ.get("MANUAL_QEMU_CACHE_DIR", ROOT / "build/manual-qemu-cache")).resolve()
        self.artifacts = self.cache / "current"
        configured_packages = os.environ.get("PACKAGE_DIR")
        if not configured_packages:
            raise RuntimeError("PACKAGE_DIR is required; run the manual-kde-lab CMake target")
        self.packages = Path(configured_packages).resolve(strict=True)
        self.root_tar: Path | None = None
        self.created = False

    def build(self) -> None:
        self.cache.mkdir(parents=True, exist_ok=True)
        run(["docker", "build", "-t", IMAGE, "-f", str(ROOT / "tests/manual/Dockerfile"), str(ROOT / "tests/manual")])
        if len(list(self.packages.glob("*.pkg.tar.zst"))) < 2:
            raise RuntimeError(f"base and KDE packages are missing in {self.packages}")

    def export_root(self) -> tuple[Path, str]:
        key = image_id(IMAGE)
        root_tar = self.cache / f"guest-root-{key}.tar"
        if not root_tar.exists():
            container = run(["docker", "create", IMAGE, "/usr/bin/true"], capture=True).stdout.strip()
            temporary = root_tar.with_suffix(".tmp")
            try:
                run(["docker", "export", "--output", str(temporary), container])
            finally:
                run(["docker", "rm", "-f", container], check=False)
            temporary.replace(root_tar)
        self.root_tar = root_tar
        return root_tar, key

    def prepare(self) -> None:
        root_tar, key = self.export_root()
        self.artifacts.mkdir(parents=True, exist_ok=True)
        run(["docker", "run", "--rm", "--privileged", "--network=none",
             "-e", "QEMU_ROOTFS_CACHE_DIR=/cache", "-e", f"QEMU_ROOTFS_CACHE_KEY={key}",
             "-e", f"QEMU_ROOTFS_TAR=/cache/{root_tar.name}", "-e", "QEMU_PACKAGE_DIR=/packages",
             "-e", "MANUAL_ARTIFACT_DIR=/artifacts", "-e", f"MANUAL_HOST_UID={os.getuid()}",
             "-e", f"MANUAL_HOST_GID={os.getgid()}", "-v", f"{ROOT}:/work:ro",
             "-e", f"MANUAL_HOST_CACHE_DIR={self.cache}",
             "-e", f"MANUAL_HOST_ARTIFACT_DIR={self.artifacts}",
             "-v", f"{self.cache}:/cache", "-v", f"{self.artifacts}:/artifacts",
             "-v", f"{self.packages}:/packages:ro", "-w", "/work", IMAGE,
             "python3", "/work/tests/manual/manual_vm.py", "--prepare-only"])

    def xml(self) -> str:
        a = self.artifacts
        path = lambda value: escape(str(value))
        return f"""<domain type='kvm'>
  <name>{DOMAIN}</name>
  <memory unit='MiB'>3072</memory><vcpu>4</vcpu>
  <cpu mode='host-passthrough' check='none'/>
  <os><type arch='x86_64' machine='q35'>hvm</type><kernel>{path(a / 'vmlinuz')}</kernel>
    <cmdline>root=/dev/vda rw rootfstype=ext4 quiet console=ttyS0 systemd.unit=graphical.target</cmdline></os>
  <features><acpi/><apic/></features>
  <clock offset='utc'/><on_poweroff>destroy</on_poweroff><on_reboot>restart</on_reboot><on_crash>destroy</on_crash>
  <devices>
    <emulator>/usr/bin/qemu-system-x86_64</emulator>
    <disk type='file' device='disk'><driver name='qemu' type='qcow2'/><source file='{path(a / 'root.qcow2')}'/><target dev='vda' bus='virtio'/><serial>bb-manual-root</serial></disk>
    <disk type='file' device='disk'><driver name='qemu' type='raw'/><source file='{path(a / 'setup.img')}'/><target dev='vdb' bus='virtio'/><readonly/><serial>bb-manual-setup</serial></disk>
    <disk type='file' device='disk'><driver name='qemu' type='qcow2'/><source file='{path(a / 'source.qcow2')}'/><target dev='vdc' bus='virtio'/><serial>bb-manual-source</serial></disk>
    <disk type='file' device='disk'><driver name='qemu' type='qcow2'/><source file='{path(a / 'target.qcow2')}'/><target dev='vdd' bus='virtio'/><serial>bb-manual-target</serial></disk>
    <disk type='file' device='disk'><driver name='qemu' type='qcow2'/><source file='{path(a / 'blank.qcow2')}'/><target dev='vde' bus='virtio'/><serial>bb-manual-blank</serial></disk>
    <disk type='file' device='disk'><driver name='qemu' type='qcow2'/><source file='{path(a / 'partitioned.qcow2')}'/><target dev='vdf' bus='virtio'/><serial>bb-manual-partitioned</serial></disk>
    <disk type='file' device='disk'><driver name='qemu' type='qcow2'/><source file='{path(a / 'incompatible.qcow2')}'/><target dev='vdg' bus='virtio'/><serial>bb-manual-incompatible</serial></disk>
    <disk type='file' device='disk'><driver name='qemu' type='qcow2'/><source file='{path(a / 'adopt.qcow2')}'/><target dev='vdh' bus='virtio'/><serial>bb-manual-adopt</serial></disk>
    <controller type='usb' model='qemu-xhci'/><input type='tablet' bus='usb'/>
    <graphics type='spice' autoport='yes'><listen type='none'/></graphics>
    <video><model type='virtio' heads='1' primary='yes'/></video>
    <channel type='spicevmc'><target type='virtio' name='com.redhat.spice.0'/></channel>
    <rng model='virtio'><backend model='random'>/dev/urandom</backend></rng>
    <serial type='file'><source path='{path(a / 'console.log')}'/><target port='0'/></serial>
    <console type='file'><source path='{path(a / 'console.log')}'/><target type='serial' port='0'/></console>
    <memballoon model='virtio'/>
  </devices>
</domain>"""

    def virsh(self, *arguments: str, capture: bool = False, check: bool = True) -> subprocess.CompletedProcess[str]:
        return run(["virsh", "-c", self.connection, *arguments], capture=capture, check=check)

    def start(self) -> None:
        if self.virsh("dominfo", DOMAIN, capture=True, check=False).returncode == 0:
            raise RuntimeError(f"domain {DOMAIN} already exists; close it before starting a new laboratory")
        with tempfile.NamedTemporaryFile("w", suffix=".xml", delete=False) as file:
            file.write(self.xml())
            xml_path = Path(file.name)
        try:
            self.virsh("create", str(xml_path))
            self.created = True
        finally:
            xml_path.unlink(missing_ok=True)
        if self.viewer:
            subprocess.Popen(["virt-manager", "--connect", self.connection, "--show-domain-console", DOMAIN],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"Started {DOMAIN} with prepared test disks and backup versions.")
        self.wait_until_ready()
        print("The laboratory is ready. Desktop and sudo credentials: tester / tester")
        print("Commands: connect, disconnect, status, quit")

    def wait_until_ready(self, timeout: float = 600.0) -> None:
        console = self.artifacts / "console.log"
        announced: set[str] = set()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = console.read_text(errors="replace") if console.exists() else ""
            for line in text.splitlines():
                marker = "MANUAL_LAB_STAGE "
                if marker in line:
                    stage = line.split(marker, 1)[1].strip()
                    if stage not in announced:
                        print(f"  guest: {stage}")
                        announced.add(stage)
            if "MANUAL_LAB_READY" in text:
                return
            if "COMMAND_FAILED" in text:
                lines = text.splitlines()
                failure = "\n".join(lines[-30:])
                raise RuntimeError(f"guest setup failed; console tail follows:\n{failure}")
            if self.virsh("domstate", DOMAIN, capture=True, check=False).returncode != 0:
                raise RuntimeError("the guest stopped before its setup completed")
            time.sleep(0.5)
        raise RuntimeError(f"guest setup did not finish within {timeout:.0f} seconds; inspect {console}")

    def control(self) -> None:
        connected = True
        while self.virsh("domstate", DOMAIN, check=False, capture=True).returncode == 0:
            try:
                action = input("manual-lab> ").strip().lower()
            except EOFError:
                self.virsh("console", DOMAIN)
                return
            if action == "disconnect":
                if connected:
                    self.virsh("detach-disk", DOMAIN, "vdd", "--live")
                    connected = False
                    print("The backup disk was physically disconnected.")
                else:
                    print("The backup disk is already disconnected.")
            elif action == "connect":
                if not connected:
                    self.virsh("attach-disk", DOMAIN, str(self.artifacts / "target.qcow2"), "vdd",
                               "--targetbus", "virtio", "--subdriver", "qcow2", "--serial", "bb-manual-target", "--live")
                    connected = True
                    print("The backup disk was connected.")
                else:
                    print("The backup disk is already connected.")
            elif action == "status":
                print(self.virsh("domstate", DOMAIN, capture=True).stdout.strip(),
                      "; disk:", "connected" if connected else "disconnected")
            elif action in ("quit", "exit"):
                self.virsh("shutdown", DOMAIN, check=False)
                for _ in range(20):
                    if self.virsh("domstate", DOMAIN, check=False, capture=True).returncode != 0:
                        return
                    time.sleep(0.5)
                self.virsh("destroy", DOMAIN, check=False)
                return
            elif action:
                print("Unknown command. Available: connect, disconnect, status, quit")

    def close(self) -> None:
        if self.created and self.virsh("domstate", DOMAIN, check=False, capture=True).returncode == 0:
            self.virsh("destroy", DOMAIN, check=False)

    def execute(self) -> None:
        try:
            if self.virsh("dominfo", DOMAIN, capture=True, check=False).returncode == 0:
                raise RuntimeError(f"domain {DOMAIN} already exists; close it before starting a new laboratory")
            self.build()
            self.prepare()
            self.start()
            self.control()
        finally:
            self.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Start an interactive Plasma laboratory with real LUKS and Btrfs disks.")
    parser.add_argument("--connect", help="libvirt connection URI; the system and session connections are detected by default")
    parser.add_argument("--no-viewer", action="store_true", help="do not open virt-manager automatically")
    arguments = parser.parse_args()
    for executable in ("docker", "virsh"):
        if shutil.which(executable) is None:
            raise RuntimeError(f"required executable is missing: {executable}")
    ManualLab(select_connection(arguments.connect), viewer=not arguments.no_viewer).execute()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
