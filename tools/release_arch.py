#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import gzip
import os
from pathlib import Path
import stat
import subprocess

from release_common import sha256


def normalize_tree(root: Path, epoch: int) -> None:
    for path in [root, *sorted(root.rglob("*"))]:
        os.utime(path, (epoch, epoch), follow_symlinks=False)


def write_mtree(root: Path, epoch: int) -> None:
    lines = ["#mtree", "/set type=file uid=0 gid=0 mode=644"]
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        if path.name == ".MTREE":
            continue
        relative = f"./{path.relative_to(root).as_posix()}"
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            lines.append(f"{relative} time={epoch}.0 mode={mode:o} type=link link={os.readlink(path)}")
        elif path.is_dir():
            lines.append(f"{relative} time={epoch}.0 mode={mode:o} type=dir")
        elif path.is_file():
            lines.append(
                f"{relative} time={epoch}.0 mode={mode:o} size={path.stat().st_size} "
                f"sha256digest={sha256(path)}"
            )
        else:
            raise RuntimeError(f"unsupported Arch package entry: {path}")
    with (root / ".MTREE").open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as stream:
            stream.write(("\n".join(lines) + "\n").encode())


def build_arch_package(root: Path, stage: Path, destination: Path, version: str,
                       arch: str, epoch: int, *, kde: bool = False) -> Path:
    package_name = "btrfs-backup-kde" if kde else "btrfs-backup"
    installed_size = sum(path.stat().st_size for path in stage.rglob("*") if path.is_file())
    dependencies = (
        [f"btrfs-backup={version}-1", "kcoreaddons", "ki18n", "kirigami", "kjobwidgets",
         "kservice", "libplasma", "qt6-base", "qt6-declarative"]
        if kde else
        ["btrfs-progs>=6.0", "coreutils", "cryptsetup", "gcc-libs", "polkit", "systemd",
         "systemd-libs", "util-linux"]
    )
    description = ("Plasma status widget and progress monitor for btrfs-backup" if kde else
                   "Verified Btrfs send/receive backups to an encrypted removable target")
    metadata = [
        f"pkgname = {package_name}", "pkgbase = btrfs-backup", "xdata = pkgtype=pkg",
        f"pkgver = {version}-1", f"pkgdesc = {description}", f"builddate = {epoch}",
        "packager = local reproducible build", f"size = {installed_size}", f"arch = {arch}",
        "license = GPL-3.0-or-later", *[f"depend = {item}" for item in dependencies],
    ]
    if not kde:
        metadata.append("optdepend = btrfs-backup-kde: Plasma status widget")
    (stage / ".PKGINFO").write_text("\n".join(metadata) + "\n")
    normalize_tree(stage, epoch)
    write_mtree(stage, epoch)
    os.utime(stage / ".MTREE", (epoch, epoch))
    entries = sorted(path.name for path in stage.iterdir())
    subprocess.run([
        "bsdtar", "--uid", "0", "--gid", "0", "--uname", "root", "--gname", "root",
        "--zstd", "-cf", str(destination), *entries,
    ], cwd=stage, check=True)
    return destination
