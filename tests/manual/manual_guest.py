#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


SETUP = Path("/run/btrfs-backup-manual-setup")
SOURCE_DEVICE = Path("/dev/disk/by-id/virtio-bb-manual-source")
TARGET_DEVICE = Path("/dev/disk/by-id/virtio-bb-manual-target")
SOURCE_ROOT = Path("/srv/manual-source")
KEY_FILE = Path("/root/.config/btrfs-backup/manual-target.key")
PROFILE = "manual-lab"


def run(arguments: list[str], *, input_text: str | None = None, check: bool = True,
        timeout: float | None = 180.0) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            arguments,
            check=False,
            text=True,
            input=input_text,
            capture_output=True,
            timeout=timeout,
            env={**os.environ, "LC_ALL": "C.UTF-8"},
        )
    except subprocess.TimeoutExpired:
        print(f"COMMAND_FAILED {arguments!r} timed out after {timeout} seconds", file=sys.stderr)
        raise
    if check and result.returncode != 0:
        print(f"COMMAND_FAILED {arguments!r} exit={result.returncode}", file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        result.check_returncode()
    return result


def write(path: Path, contents: str, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents)
    path.chmod(mode)


def wait_for(path: Path, timeout: float = 60.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        time.sleep(0.2)
    raise RuntimeError(f"device did not appear: {path}")


def stage(message: str) -> None:
    print(f"MANUAL_LAB_STAGE {message}", file=sys.stderr, flush=True)


def install_packages() -> None:
    packages = sorted(SETUP.glob("*.pkg.tar.zst"))
    if len(packages) != 2:
        raise RuntimeError("the setup disk must contain base and KDE packages")
    run(["pacman", "-U", "--noconfirm", *map(str, packages)])


def configure_desktop() -> None:
    if run(["id", "tester"], check=False).returncode != 0:
        run(["useradd", "-m", "-G", "wheel", "-s", "/bin/bash", "tester"])
    run(["chpasswd"], input_text="tester:tester\n")
    write(Path("/etc/sudoers.d/manual-lab"), "%wheel ALL=(ALL:ALL) ALL\n", 0o440)
    write(Path("/etc/sddm.conf.d/manual-lab.conf"), """[Autologin]
User=tester
Session=plasma.desktop

[Theme]
Current=breeze
""")
    write(Path("/etc/locale.conf"), "LANG=en_US.UTF-8\n")
    locale_gen = Path("/etc/locale.gen")
    text = locale_gen.read_text()
    text = text.replace("#en_US.UTF-8 UTF-8", "en_US.UTF-8 UTF-8")
    locale_gen.write_text(text)
    run(["locale-gen"])
    graphical = Path("/etc/systemd/system/default.target")
    graphical.unlink(missing_ok=True)
    graphical.symlink_to("/usr/lib/systemd/system/graphical.target")
    display_manager = Path("/etc/systemd/system/display-manager.service")
    display_manager.unlink(missing_ok=True)
    display_manager.symlink_to("/usr/lib/systemd/system/sddm.service")


def mount_source() -> None:
    wait_for(SOURCE_DEVICE)
    SOURCE_ROOT.mkdir(parents=True, exist_ok=True)
    run(["mount", str(SOURCE_DEVICE), str(SOURCE_ROOT)])


def install_prepared_profile() -> None:
    wait_for(TARGET_DEVICE)
    KEY_FILE.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SETUP / "manual-target.key", KEY_FILE)
    KEY_FILE.chmod(0o600)
    run(["btrfs-backupctl", "profile", "save", "--file", str(SETUP / "profile.json")])
    stage("mounting-prepared-backup-target")
    run(["systemctl", "start", f"btrfs-backup-target@{PROFILE}.service"])


def make_small_destination() -> None:
    image = Path("/var/lib/manual-lab/small-destination.img")
    image.parent.mkdir(parents=True, exist_ok=True)
    run(["truncate", "-s", "24M", str(image)])
    run(["mkfs.ext4", "-q", "-F", str(image)])
    destination = Path("/home/tester/Small destination — no space")
    destination.mkdir(parents=True, exist_ok=True)
    run(["mount", "-o", "loop", str(image), str(destination)])
    shutil.chown(destination, user="tester", group="tester")


def desktop_files() -> None:
    desktop = Path("/home/tester/Desktop")
    desktop.mkdir(parents=True, exist_ok=True)
    launchers = {
        "01-Previous-versions.desktop": ("Previous versions", "folder-sync", f"dolphin btrfsbackup:/{PROFILE}/"),
        "02-Backup-settings.desktop": ("Backup settings", "preferences-system-backup", f"systemsettings kcm_btrfsbackup --args {PROFILE}"),
        "03-Backup-widget.desktop": ("Backup widget", "drive-harddisk", "plasmawindowed org.btrfsbackup.plasmoid"),
        "04-Source-files.desktop": ("Source files", "folder-documents", f"dolphin {SOURCE_ROOT / 'home/tester'}"),
        "05-Service-logs.desktop": ("Service logs", "utilities-terminal", "konsole -e journalctl -f -u btrfs-backupd.service -u 'btrfs-backup@*'"),
        "06-Prepare-cancellation.desktop": ("Prepare large transfer", "document-new", "/usr/local/bin/btrfs-backup-manual-change"),
    }
    for filename, (name, icon, command) in launchers.items():
        write(desktop / filename, f"[Desktop Entry]\nType=Application\nName={name}\nIcon={icon}\nExec={command}\nTerminal=false\n", 0o755)
    guide = """# btrfs-backup test laboratory

User and sudo password: `tester`. LUKS recovery passphrase: `manual-backup-key`.

Two versions of “Home files” and “Web server” are ready. The shortcuts open
Dolphin, settings, the widget, and logs. “Small destination — no space” has
only 24 MiB and tests rejection of a large-file restore.

Enter `disconnect` or `connect` in the host terminal to physically remove or
attach the backup disk. Restart the script to restore the initial state.
"""
    write(desktop / "README.md", guide)
    restore = Path("/home/tester/Restore conflict")
    restore.mkdir(parents=True, exist_ok=True)
    write(restore / "report.txt", "File present before the restore.\n")
    shutil.chown(restore, user="tester", group="tester")
    shutil.chown(restore / "report.txt", user="tester", group="tester")
    for path in desktop.iterdir():
        shutil.chown(path, user="tester", group="tester")
    shutil.chown(desktop, user="tester", group="tester")


def main() -> int:
    sys.stderr = open("/dev/ttyS0", "a", buffering=1)
    stage("installing-packages")
    install_packages()
    stage("configuring-desktop")
    configure_desktop()
    stage("mounting-prepared-source")
    mount_source()
    stage("installing-prepared-profile")
    install_prepared_profile()
    stage("preparing-restore-destinations")
    make_small_destination()
    shutil.copy2(SETUP / "manual_change.py", "/usr/local/bin/btrfs-backup-manual-change")
    Path("/usr/local/bin/btrfs-backup-manual-change").chmod(0o755)
    desktop_files()
    run(["systemctl", "start", "btrfs-backupd.service"])
    Path("/run/manual-lab-ready").touch()
    print("MANUAL_LAB_READY", file=sys.stderr, flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
