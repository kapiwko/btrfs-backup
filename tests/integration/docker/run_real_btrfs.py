#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
CONTAINER_WORKDIR = "/work"


def run(arguments: list[str], *, capture_output: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        arguments,
        check=True,
        text=True,
        capture_output=capture_output,
        env={**os.environ, "LC_ALL": "C"},
    )


def executable_from_environment(name: str, relative_default: str) -> Path:
    executable = Path(os.environ.get(name, ROOT / relative_default)).resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise RuntimeError(f"integration executable is missing or not executable: {executable}")
    return executable


class RealBtrfsContainer:
    def __init__(self) -> None:
        self.image = os.environ.get("IMAGE_NAME", "btrfs-backup-real-test:local")
        self.build_image = os.environ.get("BUILD_IMAGE_NAME", "btrfs-backup-build-test:local")
        self.package_builder = os.environ.get("PACKAGE_BUILDER", "local")
        self.build_jobs = os.environ.get("BUILD_JOBS", str(os.cpu_count() or 2))
        self.browse_client = executable_from_environment(
            "BTRFSBACKUP_BROWSE_SESSION_CLIENT",
            "build/tests/integration/btrfsbackup-integration-browse-session-client",
        )
        self.provisioning_client = executable_from_environment(
            "BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT",
            "build/tests/integration/btrfsbackup-integration-device-provisioning-client",
        )
        self.real_tests = executable_from_environment(
            "BTRFSBACKUP_REAL_BTRFS_TESTS",
            "build/tests/integration/btrfsbackup-real-btrfs-tests",
        )
        self.installed_runtime_tests = executable_from_environment(
            "BTRFSBACKUP_REAL_INSTALLED_RUNTIME_TESTS",
            "build/tests/integration/btrfsbackup-real-installed-runtime-tests",
        )
        self.mapper_lifecycle_tests = executable_from_environment(
            "BTRFSBACKUP_REAL_MAPPER_LIFECYCLE_TESTS",
            "build/tests/integration/btrfsbackup-real-mapper-lifecycle-tests",
        )
        self.container_id: str | None = None
        self.package_temporary_root: Path | None = None

    def close(self) -> None:
        if self.container_id is not None:
            subprocess.run(
                ["docker", "rm", "-f", self.container_id],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            self.container_id = None
        if self.package_temporary_root is not None:
            shutil.rmtree(self.package_temporary_root, ignore_errors=True)
            self.package_temporary_root = None

    def build_runtime_image(self) -> None:
        run(
            [
                "docker",
                "build",
                "-t",
                self.image,
                "-f",
                str(ROOT / "tests/integration/docker/Dockerfile"),
                str(ROOT / "tests/integration/docker"),
            ]
        )

    def prepare_packages(self) -> Path:
        configured = os.environ.get("PACKAGE_DIR")
        if configured:
            package_root = Path(configured).resolve(strict=True)
        else:
            self.package_temporary_root = Path(
                tempfile.mkdtemp(prefix="btrfs-backup-real-packages.", dir="/tmp")
            )
            package_root = self.package_temporary_root / "dist"
            if self.package_builder == "local":
                run(
                    [
                        str(ROOT / "tools/build-release.sh"),
                        "--target",
                        "arch-base",
                        "--skip-tests",
                        "--build-dir",
                        str(ROOT / "build/integration-package"),
                        "--dist-dir",
                        str(package_root),
                    ]
                )
            elif self.package_builder == "docker":
                self.build_packages_in_container()
            else:
                raise RuntimeError(f"unsupported PACKAGE_BUILDER: {self.package_builder}")
        if not list(package_root.glob("btrfs-backup-[0-9]*.pkg.tar.zst")):
            raise RuntimeError(f"no base Arch package found in {package_root}")
        return package_root

    def build_packages_in_container(self) -> None:
        assert self.package_temporary_root is not None
        run(
            [
                "docker",
                "build",
                "-t",
                self.build_image,
                "-f",
                str(ROOT / "tests/integration/docker/Dockerfile.build"),
                str(ROOT / "tests/integration/docker"),
            ]
        )
        user = f"{os.getuid()}:{os.getgid()}"
        run(
            [
                "docker",
                "run",
                "--rm",
                "--network=none",
                "--user",
                user,
                "--tmpfs",
                f"/run:uid={os.getuid()},gid={os.getgid()},mode=0755",
                "-e",
                f"BUILD_JOBS={self.build_jobs}",
                "-e",
                "HOME=/tmp",
                "-v",
                f"{ROOT}:{CONTAINER_WORKDIR}:ro",
                "-v",
                f"{self.package_temporary_root}:/artifacts",
                "-w",
                CONTAINER_WORKDIR,
                self.build_image,
                f"{CONTAINER_WORKDIR}/tools/build-release.sh",
                "--target",
                "arch-base",
                "--skip-tests",
                "--dist-dir",
                "/artifacts/dist",
            ]
        )

    def start(self, package_root: Path) -> None:
        started = run(
            [
                "docker",
                "run",
                "-d",
                "--rm",
                "--privileged",
                "--cgroupns=host",
                "-e",
                f"BUILD_JOBS={self.build_jobs}",
                "--tmpfs",
                "/run",
                "--tmpfs",
                "/tmp:exec,mode=1777",
                "-v",
                f"{ROOT}:{CONTAINER_WORKDIR}:ro",
                "-v",
                f"{package_root}:/packages:ro",
                "-v",
                f"{self.browse_client}:/opt/btrfsbackup-browse-session-client:ro",
                "-v",
                f"{self.provisioning_client}:/opt/btrfsbackup-device-provisioning-client:ro",
                "-v",
                f"{self.real_tests}:/opt/btrfsbackup-real-btrfs-tests:ro",
                "-v",
                f"{self.installed_runtime_tests}:/opt/btrfsbackup-real-installed-runtime-tests:ro",
                "-v",
                f"{self.mapper_lifecycle_tests}:/opt/btrfsbackup-real-mapper-lifecycle-tests:ro",
                "-w",
                CONTAINER_WORKDIR,
                self.image,
                "/sbin/init",
            ],
            capture_output=True,
        )
        self.container_id = started.stdout.strip()
        if not self.container_id:
            raise RuntimeError("Docker did not return a container identifier")

    def execute_tests(self) -> None:
        assert self.container_id is not None
        run(
            [
                "docker",
                "exec",
                "-e",
                f"BUILD_JOBS={self.build_jobs}",
                "-e",
                "BTRFSBACKUP_PACKAGE_DIR=/packages",
                "-e",
                "BTRFSBACKUP_BROWSE_SESSION_CLIENT=/opt/btrfsbackup-browse-session-client",
                "-e",
                "BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT=/opt/btrfsbackup-device-provisioning-client",
                "-e",
                "BTRFSBACKUP_REAL_BTRFS_TESTS=/opt/btrfsbackup-real-btrfs-tests",
                "-e",
                "BTRFSBACKUP_REAL_INSTALLED_RUNTIME_TESTS=/opt/btrfsbackup-real-installed-runtime-tests",
                "-e",
                "BTRFSBACKUP_REAL_MAPPER_LIFECYCLE_TESTS=/opt/btrfsbackup-real-mapper-lifecycle-tests",
                "-e",
                "BTRFSBACKUP_REAL_BTRFS_CONTAINER=1",
                "-w",
                CONTAINER_WORKDIR,
                self.container_id,
                f"{CONTAINER_WORKDIR}/tests/integration/docker/real-btrfs-test.sh",
            ]
        )

    def run(self) -> None:
        try:
            self.build_runtime_image()
            package_root = self.prepare_packages()
            self.start(package_root)
            self.execute_tests()
        finally:
            self.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Build and run the real Btrfs integration test in a privileged Docker container. "
            "All loop-backed Btrfs and LUKS filesystems are disposable."
        )
    )
    parser.parse_args()

    def terminate(_signal_number: int, _frame: object) -> None:
        raise InterruptedError("container runner interrupted")

    signal.signal(signal.SIGTERM, terminate)
    RealBtrfsContainer().run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
