#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


PROFILE_ROOT = Path("/etc/btrfs-backup/profiles")
BACKUPCTL = Path("/usr/bin/btrfs-backupctl")


def run(
    arguments: list[str], *, check: bool = True, env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, check=check, text=True, capture_output=True, env=env)


def profile(schema_version: int) -> dict[str, object]:
    result: dict[str, object] = {
        "schemaVersion": schema_version,
        "profileId": "default",
        "name": "Migration transaction",
        "enabled": True,
        "target": {
            "device": "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555",
            "luksUuid": "11111111-2222-3333-4444-555555555555",
            "btrfsUuid": "66666666-7777-8888-9999-aaaaaaaaaaaa",
            "mapperName": "backupdisk",
            "activation": {"mode": "askPassword"},
        },
        "sources": [{
            "id": "home",
            "name": "Home",
            "enabled": True,
            "subvolume": "/home",
            "localSnapshotDir": "/.snapshots/btrfs-backup/home",
            "remoteSubdir": "home",
            "remoteRetention": 2,
            "localRetention": 2,
        }],
    }
    if schema_version == 4:
        result["configurationGeneration"] = "0123456789abcdef0123456789abcdef"
    return result


def install_profile(document: dict[str, object]) -> Path:
    path = PROFILE_ROOT / "default/profile.json"
    path.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n")
    path.chmod(0o600)
    return path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_failed_legacy_upgrade(package: Path) -> None:
    path = install_profile(profile(3))
    original_profile = path.read_bytes()
    original_binary = sha256(BACKUPCTL)
    original_version = run(["pacman", "-Q", "btrfs-backup"]).stdout
    result = run(["pacman", "-U", "--noconfirm", str(package)], check=False)
    diagnostic = result.stdout + result.stderr
    if result.returncode == 0 or "failed to run transaction hooks" not in diagnostic:
        raise RuntimeError(f"legacy profile did not abort the pacman transaction:\n{diagnostic}")
    if path.read_bytes() != original_profile:
        raise RuntimeError("failed migration transaction modified the legacy profile")
    if sha256(BACKUPCTL) != original_binary or run(["pacman", "-Q", "btrfs-backup"]).stdout != original_version:
        raise RuntimeError("failed migration transaction replaced the installed package")


def migrate_and_save_profile(temporary: Path) -> None:
    exported = temporary / "exported"
    run([str(BACKUPCTL), "profile", "export-v4", "--all", "--output-dir", str(exported)])
    exported_profile = exported / "profiles/default/profile.json"
    if json.loads(exported_profile.read_text()).get("schemaVersion") != 4:
        raise RuntimeError("migration export did not produce a schema-v4 profile")
    saved_root = temporary / "saved"
    environment = {
        **os.environ,
        "BTRFS_BACKUP_ETC_ROOT": str(saved_root / "etc"),
        "BTRFS_BACKUP_UDEV_ROOT": str(saved_root / "udev"),
        "BTRFS_BACKUP_SYSTEMD_ROOT": str(saved_root / "systemd"),
        "BTRFS_BACKUP_PUBLIC_ROOT": str(saved_root / "public"),
    }
    run([str(BACKUPCTL), "profile", "save", "--file", str(exported_profile)], env=environment)
    saved_profile = saved_root / "etc/profiles/default/profile.json"
    if not saved_profile.is_file():
        raise RuntimeError("schema-v4 migration export was not saved")
    install_profile(json.loads(saved_profile.read_text()))


def exercise(package_directory: Path) -> None:
    if os.geteuid() != 0 or os.environ.get("BTRFSBACKUP_REAL_BTRFS_CONTAINER") != "1":
        raise RuntimeError("Arch migration transactions require the disposable integration container")
    packages = sorted(package_directory.glob("btrfs-backup-[0-9]*.pkg.tar.zst"))
    if len(packages) != 1:
        raise RuntimeError(f"expected one Arch base package, found {len(packages)}")
    package = packages[0]
    run(["pacman", "-U", "--noconfirm", str(package)])
    hook = Path("/usr/share/libalpm/hooks/90-btrfs-backup-v4-migration.hook")
    if not hook.is_file() or "AbortOnFail" not in hook.read_text():
        raise RuntimeError("installed package omitted the fail-closed migration hook")
    try:
        require_failed_legacy_upgrade(package)
        with tempfile.TemporaryDirectory(prefix="btrfs-backup-arch-migration.", dir="/tmp") as directory:
            migrate_and_save_profile(Path(directory))
        run([str(BACKUPCTL), "upgrade", "preflight"])
        run(["pacman", "-U", "--noconfirm", str(package)])
        shutil.rmtree(PROFILE_ROOT / "default")
        run(["pacman", "-U", "--noconfirm", str(package)])
    finally:
        shutil.rmtree(PROFILE_ROOT / "default", ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Exercise fail-closed Arch profile migration transactions.")
    parser.add_argument("package_directory", type=Path)
    arguments = parser.parse_args()
    exercise(arguments.package_directory)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
