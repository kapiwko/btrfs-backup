#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from pathlib import Path
import shutil

from release_common import deterministic_tar_gz


def render(source: Path, destination: Path, values: dict[str, str]) -> None:
    content = source.read_text()
    for name, value in values.items():
        content = content.replace(f"@{name}@", value)
    if "@VERSION@" in content or "@ARCH@" in content or "@SOURCE_SHA256@" in content:
        raise RuntimeError(f"unresolved release placeholder in {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(content)


def build_definition(root: Path, work: Path, dist: Path, target: str, version: str,
                     arch: str, source_sha256: str, epoch: int) -> Path:
    names = {
        "rpm": f"btrfs-backup-{version}-rpm-packaging",
        "nix": f"btrfs-backup-{version}-nix-packaging",
        "ebuild": f"btrfs-backup-{version}-ebuild",
        "pkgbuild": f"btrfs-backup-{version}-pkgbuild",
    }
    package_name = names[target]
    package = work / package_name
    package.mkdir(parents=True)
    values = {"VERSION": version, "ARCH": arch, "SOURCE_SHA256": source_sha256}
    if target == "rpm":
        render(root / "packaging/rpm/btrfs-backup.spec.in", package / "btrfs-backup.spec", values)
    elif target == "nix":
        render(root / "packaging/nix/package.nix.in", package / "package.nix", values)
        shutil.copy2(root / "packaging/nix/README.md", package / "README.md")
    elif target == "ebuild":
        render(root / "packaging/gentoo/btrfs-backup.ebuild.in",
               package / f"btrfs-backup-{version}.ebuild", values)
        (package / "source.SHA256SUM").write_text(
            f"{source_sha256}  btrfs-backup-{version}.tar.gz\n"
        )
    else:
        render(root / "packaging/arch/PKGBUILD.in", package / "PKGBUILD", values)
        render(root / "packaging/arch/SRCINFO.in", package / ".SRCINFO", values)
        shutil.copy2(root / "packaging/arch/btrfs-backup.install",
                     package / "btrfs-backup.install")
    destination = dist / f"{package_name}.tar.gz"
    deterministic_tar_gz(package, destination, epoch)
    return destination
