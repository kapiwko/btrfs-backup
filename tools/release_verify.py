#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from pathlib import Path
import subprocess
import tarfile
import tempfile


BASE_PATHS = {
    "etc/btrfs-backup/hooks.d",
    "usr/bin/btrfs-backup",
    "usr/bin/btrfs-backupctl",
    "usr/bin/btrfs-backupd",
    "usr/bin/btrfs-backup-device-preparation",
    "usr/lib/systemd/system/btrfs-backup@.service",
    "usr/share/dbus-1/system-services/io.github.btrfsbackup.Manager1.service",
    "usr/share/polkit-1/actions/io.github.btrfsbackup.policy",
}


def require_paths(entries: set[str], expected: set[str], artifact: Path) -> None:
    normalized = {entry.rstrip("/") for entry in entries}
    missing = sorted(expected - normalized)
    if missing:
        raise RuntimeError(f"{artifact.name} is missing: {', '.join(missing)}")


def archive_entries(artifact: Path) -> set[str]:
    result = subprocess.run(["bsdtar", "-tf", str(artifact)], check=True,
                            text=True, capture_output=True)
    return set(result.stdout.splitlines())


def verify_arch(artifact: Path) -> None:
    entries = archive_entries(artifact)
    require_paths(entries, BASE_PATHS | {".INSTALL", ".MTREE", ".PKGINFO"}, artifact)
    with tempfile.TemporaryDirectory(prefix="btrfs-backup-package-audit.", dir="/tmp") as temporary:
        root = Path(temporary)
        subprocess.run(["bsdtar", "-xf", str(artifact), "-C", str(root)], check=True)
        subprocess.run(["bash", "-n", str(root / ".INSTALL")], check=True)
        for command in ("btrfs-backup", "btrfs-backupctl", "btrfs-backupd"):
            subprocess.run([str(root / "usr/bin" / command), "--help"], check=True,
                           stdout=subprocess.DEVNULL)
        rendered = list((root / "usr/lib/systemd/system").glob("*"))
        rendered.append(root / "usr/share/dbus-1/system-services/io.github.btrfsbackup.Manager1.service")
        if any("@BTRFSBACKUP_" in path.read_text() for path in rendered):
            raise RuntimeError(f"{artifact.name} contains unresolved installation tokens")


def verify_install_tarball(artifact: Path) -> None:
    with tarfile.open(artifact, "r:gz") as archive:
        require_paths(set(archive.getnames()), BASE_PATHS, artifact)


def verify_native_package(artifact: Path) -> None:
    if artifact.suffix == ".deb":
        subprocess.run(["dpkg-deb", "--info", str(artifact)], check=True,
                       stdout=subprocess.DEVNULL)
    elif artifact.suffix == ".rpm":
        subprocess.run(["rpm", "-qplp", str(artifact)], check=True,
                       stdout=subprocess.DEVNULL)


def verify_artifacts(artifacts: list[Path]) -> None:
    for artifact in artifacts:
        if artifact.name.endswith(".pkg.tar.zst") and "-kde-" not in artifact.name:
            verify_arch(artifact)
        elif artifact.name.endswith("-install.tar.gz"):
            verify_install_tarball(artifact)
        elif artifact.suffix in (".deb", ".rpm"):
            verify_native_package(artifact)
