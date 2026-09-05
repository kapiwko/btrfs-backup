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

from hotplug_vm import HotplugVm


ROOT = Path(__file__).resolve().parents[2]


def run(arguments: list[str], *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        arguments,
        check=True,
        text=True,
        capture_output=capture,
        env={**os.environ, "LC_ALL": "C"},
    )


class QemuHotplugHost:
    def __init__(self) -> None:
        self.runtime_image = os.environ.get("IMAGE_NAME", "btrfs-backup-real-test:local")
        self.qemu_image = os.environ.get("QEMU_IMAGE_NAME", "btrfs-backup-qemu-test:local")
        self.build_image = os.environ.get("BUILD_IMAGE_NAME", "btrfs-backup-build-test:local")
        self.package_builder = os.environ.get("PACKAGE_BUILDER", "local")
        self.build_jobs = os.environ.get("BUILD_JOBS", str(os.cpu_count() or 2))
        self.cache = Path(os.environ.get("QEMU_CACHE_DIR", ROOT / "build/qemu-cache")).resolve()
        configured_client = os.environ.get(
            "BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT",
            ROOT / "build/tests/integration/btrfsbackup-integration-device-provisioning-client",
        )
        self.client = Path(configured_client).resolve()
        if not self.client.is_file() or not os.access(self.client, os.X_OK):
            raise RuntimeError(f"device-provisioning client is not executable: {self.client}")
        self.package_root: Path | None = None
        self.export_container: str | None = None
        self.export_temporary: Path | None = None

    def close(self) -> None:
        if self.export_container is not None:
            subprocess.run(
                ["docker", "rm", "-f", self.export_container],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
        if self.export_temporary is not None:
            self.export_temporary.unlink(missing_ok=True)
        if self.package_root is not None:
            shutil.rmtree(self.package_root, ignore_errors=True)

    def build_images(self) -> None:
        run(["docker", "build", "-t", self.runtime_image, "-f",
             str(ROOT / "tests/integration/docker/Dockerfile"), str(ROOT / "tests/integration/docker")])
        run(["docker", "build", "--build-arg", f"BASE_IMAGE={self.runtime_image}",
             "-t", self.qemu_image, "-f", str(ROOT / "tests/qemu/Dockerfile"), str(ROOT / "tests/qemu")])

    def prepare_packages(self) -> Path:
        configured = os.environ.get("PACKAGE_DIR")
        if configured:
            package_dir = Path(configured).resolve(strict=True)
        else:
            self.package_root = Path(tempfile.mkdtemp(prefix="btrfs-backup-qemu-packages.", dir="/tmp"))
            package_dir = self.package_root / "dist"
            if self.package_builder == "local":
                run([str(ROOT / "tools/release.py"), "--target", "arch-base", "--skip-tests",
                     "--build-dir", str(ROOT / "build/integration-package"), "--dist-dir", str(package_dir)])
            elif self.package_builder == "docker":
                self.build_packages_in_docker()
            else:
                raise RuntimeError(f"unsupported PACKAGE_BUILDER: {self.package_builder}")
        if not list(package_dir.glob("btrfs-backup-[0-9]*.pkg.tar.zst")):
            raise RuntimeError(f"no base Arch package found in {package_dir}")
        return package_dir

    def build_packages_in_docker(self) -> None:
        assert self.package_root is not None
        run(["docker", "build", "-t", self.build_image, "-f",
             str(ROOT / "tests/integration/docker/Dockerfile.build"), str(ROOT / "tests/integration/docker")])
        run(["docker", "run", "--rm", "--network=none", "--user", f"{os.getuid()}:{os.getgid()}",
             "--tmpfs", f"/run:uid={os.getuid()},gid={os.getgid()},mode=0755", "-e",
             f"BUILD_JOBS={self.build_jobs}", "-e", "HOME=/tmp", "-v", f"{ROOT}:/work:ro",
             "-v", f"{self.package_root}:/artifacts", "-w", "/work", self.build_image,
             "/work/tools/release.py", "--target", "arch-base", "--skip-tests",
             "--build-dir", "/artifacts/build", "--dist-dir", "/artifacts/dist"])

    def image_id(self, image: str) -> str:
        return run(["docker", "image", "inspect", "--format", "{{.Id}}", image], capture=True).stdout.strip().removeprefix("sha256:")

    def prepare_root_tar(self, qemu_key: str) -> Path:
        runtime_key = self.image_id(self.runtime_image)
        root_tar = self.cache / f"guest-root-{runtime_key}.tar"
        if (self.cache / f"rootfs-v2-{qemu_key}.img").exists() or root_tar.exists():
            return root_tar
        self.export_temporary = root_tar.with_suffix(".tar.tmp")
        self.export_container = run(["docker", "create", self.runtime_image, "/usr/bin/true"], capture=True).stdout.strip()
        run(["docker", "export", "--output", str(self.export_temporary), self.export_container])
        run(["docker", "rm", self.export_container])
        self.export_container = None
        self.export_temporary.replace(root_tar)
        self.export_temporary = None
        return root_tar

    def execute(self, package_dir: Path, qemu_key: str, root_tar: Path) -> None:
        run(["docker", "run", "--rm", "--privileged", "--network=none",
             "-e", "QEMU_ROOTFS_CACHE_DIR=/qemu-cache", "-e", f"QEMU_ROOTFS_CACHE_KEY={qemu_key}",
             "-e", f"QEMU_ROOTFS_TAR=/qemu-cache/{root_tar.name}", "-e", "QEMU_PACKAGE_DIR=/packages",
             "-e", "BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT=/opt/btrfsbackup-device-provisioning-client",
             "-v", f"{ROOT}:/work:ro", "-v", f"{self.cache}:/qemu-cache", "-v", f"{package_dir}:/packages:ro",
             "-v", f"{self.client}:/opt/btrfsbackup-device-provisioning-client:ro", "-w", "/work",
             self.qemu_image, "python3", "/work/tests/qemu/run_hotplug.py", "--inner"])

    def run(self) -> None:
        try:
            self.build_images()
            package_dir = self.prepare_packages()
            self.cache.mkdir(parents=True, exist_ok=True)
            qemu_key = self.image_id(self.qemu_image)
            root_tar = self.prepare_root_tar(qemu_key)
            self.execute(package_dir, qemu_key, root_tar)
            root_tar.unlink(missing_ok=True)
        finally:
            self.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Run provisioning recovery and USB hotplug in a disposable QEMU guest.")
    parser.add_argument("--inner", action="store_true", help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    if arguments.inner:
        HotplugVm().run()
    else:
        QemuHotplugHost().run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
