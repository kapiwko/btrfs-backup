#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile

from release_build import ReleaseBuild
from release_arch import build_arch_package
from release_common import (copy_source_tree, deterministic_tar_gz, deterministic_zip,
                            reset_directory, sha256, source_date_epoch, write_sbom)
from release_packaging import build_definition
from release_verify import verify_artifacts


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
    expected = {path.name: sha256(path) for path in artifacts}
    checksums.write_text("".join(f"{digest}  {name}\n" for name, digest in expected.items()))
    actual = {}
    for line in checksums.read_text().splitlines():
        digest, name = line.split("  ", maxsplit=1)
        actual[name] = digest
    for path in artifacts:
        if actual.get(path.name) != sha256(path):
            raise RuntimeError(f"checksum verification failed for {path.name}")


def run_pre_package_tests(mode: str, jobs: int) -> None:
    if mode == "skip":
        return
    if mode == "full" and os.geteuid() != 0:
        raise RuntimeError("full tests require root")
    subprocess.run(["cmake", "--preset", "default"], cwd=ROOT, check=True)
    subprocess.run(["cmake", "--build", "--preset", "default", "--parallel", str(jobs)],
                   cwd=ROOT, check=True)
    subprocess.run(["ctest", "--preset", "default", "--parallel", str(jobs),
                    "--output-on-failure"], cwd=ROOT, check=True)


def build_selected(target: str, dist: Path, temporary: Path, source_stage: Path,
                   source_archive: Path, version: str, epoch: int, build_dir: Path,
                   clean_build: bool, jobs: int) -> list[Path]:
    artifacts: list[Path] = []
    arch, deb_arch = architecture_names()
    definitions = ("rpm", "nix", "ebuild", "pkgbuild") if target == "all" else (target,)
    for definition in definitions:
        if definition in ("rpm", "nix", "ebuild", "pkgbuild"):
            artifacts.append(build_definition(
                ROOT, temporary / f"definition-{definition}", dist, definition,
                version, arch, sha256(source_archive), epoch,
            ))

    binary_targets = ("tar-install", "deb", "rpm", "arch") if target == "all" else (target,)
    if not any(item in ("tar-install", "deb", "rpm", "arch", "arch-base")
               for item in binary_targets):
        return artifacts
    source_root = source_stage if clean_build else ROOT
    actual_build_dir = temporary / "build" if clean_build else build_dir.resolve()
    release_build = ReleaseBuild(
        source_root, actual_build_dir, epoch, jobs, kde=target in ("all", "arch")
    )
    cpack_targets = {
        "tar-install": ("TGZ", f"btrfs-backup-{version}-install.tar.gz"),
        "deb": ("DEB", f"btrfs-backup_{version}-1_{deb_arch}.deb"),
        "rpm": ("RPM", f"btrfs-backup-{version}-1.{arch}.rpm"),
    }
    for binary_target in binary_targets:
        if binary_target in cpack_targets:
            generator, name = cpack_targets[binary_target]
            artifacts.append(release_build.cpack(
                generator, temporary / f"cpack-{generator.lower()}", dist / name
            ))
    if target in ("all", "arch", "arch-base"):
        base_stage = release_build.stage(temporary / "arch-base", "Unspecified")
        artifacts.append(build_arch_package(
            ROOT, base_stage, dist / f"btrfs-backup-{version}-1-{arch}.pkg.tar.zst",
            version, arch, epoch,
        ))
    if target in ("all", "arch"):
        kde_stage = release_build.stage(temporary / "arch-kde", "KDEIntegration")
        artifacts.append(build_arch_package(
            ROOT, kde_stage, dist / f"btrfs-backup-kde-{version}-1-{arch}.pkg.tar.zst",
            version, arch, epoch, kde=True,
        ))
    return artifacts


def main() -> int:
    parser = argparse.ArgumentParser(description="Build deterministic btrfs-backup release artifacts.")
    parser.add_argument(
        "--target",
        choices=("all", "source", "arch", "arch-base", "tar-install", "deb", "rpm",
                 "nix", "ebuild", "pkgbuild"), default="all",
    )
    parser.add_argument("--dist-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build" / "release")
    parser.add_argument("--clean-build", action="store_true")
    tests = parser.add_mutually_exclusive_group()
    tests.add_argument("--full-tests", action="store_const", const="full", dest="test_mode")
    tests.add_argument("--static-tests", action="store_const", const="static", dest="test_mode")
    tests.add_argument("--skip-tests", action="store_const", const="skip", dest="test_mode")
    parser.set_defaults(test_mode="skip")
    arguments = parser.parse_args()
    version = (ROOT / "VERSION").read_text().strip()
    epoch = source_date_epoch(ROOT)
    dist = arguments.dist_dir.resolve()
    jobs = max(1, int(os.environ.get("BUILD_JOBS", os.cpu_count() or 2)))
    run_pre_package_tests(arguments.test_mode, jobs)
    reset_directory(dist)
    with tempfile.TemporaryDirectory(prefix="btrfs-backup-release.", dir="/tmp") as temporary:
        temporary_root = Path(temporary)
        source_stage, artifacts = build_source(dist, temporary_root, epoch, version)
        if arguments.target != "source":
            artifacts.extend(build_selected(
                arguments.target, dist, temporary_root, source_stage, artifacts[0], version,
                epoch, arguments.build_dir, arguments.clean_build, jobs,
            ))
        verify_artifacts(artifacts)
        artifacts = write_reports(
            dist, artifacts, version, epoch, arguments.target, arguments.test_mode
        )
        write_checksums(dist, artifacts)
    print("Built release artifacts:")
    for artifact in [*artifacts, dist / "SHA256SUMS"]:
        print(f"  {artifact}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
