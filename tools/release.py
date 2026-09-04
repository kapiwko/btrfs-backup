#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import tempfile

from release_build import ReleaseBuild
from release_common import (copy_source_tree, deterministic_tar_gz, deterministic_zip,
                            reset_directory, sha256, source_date_epoch, write_sbom)


ROOT = Path(__file__).resolve().parents[1]


def architecture_names() -> tuple[str, str]:
    machine = platform.machine()
    return {
        "x86_64": ("x86_64", "amd64"),
        "aarch64": ("aarch64", "arm64"),
        "arm64": ("aarch64", "arm64"),
        "armv7l": ("armv7h", "armhf"),
        "armv7hl": ("armv7h", "armhf"),
    }.get(machine, (machine, machine))


def build_source(dist: Path, staging_root: Path, epoch: int, version: str) -> tuple[Path, list[Path]]:
    source_name = f"btrfs-backup-{version}"
    staging = staging_root / source_name
    copy_source_tree(ROOT, staging, epoch)
    archive = dist / f"{source_name}.tar.gz"
    source_zip = dist / f"{source_name}-source.zip"
    deterministic_tar_gz(staging, archive, epoch)
    deterministic_zip(staging, source_zip, epoch)
    sbom = dist / "SBOM.spdx.json"
    write_sbom(staging, sbom, version, epoch)
    return staging, [archive, source_zip, sbom]


def write_reports(dist: Path, artifacts: list[Path], version: str, epoch: int,
                  target: str, test_mode: str) -> list[Path]:
    report_data = {"schema": 1, "package": "btrfs-backup", "version": version,
                   "architecture": platform.machine(), "source_date_epoch": epoch,
                   "tests": test_mode, "target": target,
                   "artifacts": [{"path": path.name, "bytes": path.stat().st_size,
                                  "sha256": sha256(path)} for path in artifacts]}
    json_report = dist / "BUILD-REPORT.json"
    json_report.write_text(json.dumps(report_data, indent=2, sort_keys=True) + "\n")
    text_report = dist / "BUILD-REPORT.txt"
    text_report.write_text(
        f"Package: btrfs-backup\nVersion: {version}-1\nArchitecture: {platform.machine()}\n"
        f"Source date epoch: {epoch}\nTests: {test_mode}\nTarget: {target}\n"
        f"Source SHA-256: {sha256(artifacts[0])}\n"
    )
    return [*artifacts, text_report, json_report]


def write_checksums(dist: Path, artifacts: list[Path]) -> None:
    checksums = dist / "SHA256SUMS"
    checksums.write_text("".join(f"{sha256(path)}  {path.name}\n" for path in artifacts))
    for path in artifacts:
        if sha256(path) not in checksums.read_text():
            raise RuntimeError(f"checksum verification failed for {path.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build deterministic btrfs-backup release artifacts.")
    parser.add_argument("--target", choices=("source", "tar-install", "deb", "rpm"), default="source")
    parser.add_argument("--dist-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build" / "release")
    parser.add_argument("--clean-build", action="store_true")
    parser.add_argument("--skip-tests", action="store_true", help="accepted for compatibility; source has no tests")
    arguments = parser.parse_args()
    version = (ROOT / "VERSION").read_text().strip()
    epoch = source_date_epoch(ROOT)
    dist = arguments.dist_dir.resolve()
    reset_directory(dist)
    with tempfile.TemporaryDirectory(prefix="btrfs-backup-release.", dir="/tmp") as temporary:
        temporary_root = Path(temporary)
        source_stage, artifacts = build_source(dist, temporary_root, epoch, version)
        if arguments.target != "source":
            build_dir = temporary_root / "build" if arguments.clean_build else arguments.build_dir.resolve()
            source_root = source_stage if arguments.clean_build else ROOT
            jobs = max(1, int(os.environ.get("BUILD_JOBS", os.cpu_count() or 2)))
            release_build = ReleaseBuild(source_root, build_dir, epoch, jobs)
            arch, deb_arch = architecture_names()
            destinations = {
                "tar-install": ("TGZ", f"btrfs-backup-{version}-install.tar.gz"),
                "deb": ("DEB", f"btrfs-backup_{version}-1_{deb_arch}.deb"),
                "rpm": ("RPM", f"btrfs-backup-{version}-1.{arch}.rpm"),
            }
            generator, name = destinations[arguments.target]
            artifacts.append(release_build.cpack(
                generator, temporary_root / "cpack", dist / name
            ))
        artifacts = write_reports(dist, artifacts, version, epoch, arguments.target, "skip")
        write_checksums(dist, artifacts)
    print("Built release artifacts:")
    for artifact in [*artifacts, dist / "SHA256SUMS"]:
        print(f"  {artifact}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
