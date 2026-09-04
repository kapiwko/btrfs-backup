#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess


class ReleaseBuild:
    def __init__(self, source: Path, build: Path, epoch: int, jobs: int,
                 *, kde: bool = False) -> None:
        self.source = source
        self.build = build
        self.epoch = epoch
        self.jobs = jobs
        self.kde = kde
        self.configured = False
        self.built = False
        self.environment = os.environ.copy()
        self.environment.setdefault("CCACHE_DIR", str(build / ".ccache"))

    def run(self, command: list[str], *, env: dict[str, str] | None = None) -> None:
        print(f"[release] {' '.join(command)}")
        subprocess.run(command, check=True, env=env or self.environment)

    def configure(self) -> None:
        if self.configured:
            return
        command = ["cmake", "-S", str(self.source), "-B", str(self.build)]
        if not (self.build / "CMakeCache.txt").exists() and shutil.which("ninja"):
            command.extend(("-G", "Ninja"))
        command.extend((
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_INSTALL_PREFIX=/usr",
            "-DCMAKE_INSTALL_SYSCONFDIR=/etc",
            f"-DBUILD_KDE_INTEGRATION={'ON' if self.kde else 'OFF'}",
            "-DBUILD_TESTING=OFF",
        ))
        if shutil.which("ccache"):
            command.append("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
        self.run(command)
        self.configured = True

    def build_native(self) -> None:
        if self.built:
            return
        self.configure()
        self.run(["cmake", "--build", str(self.build), "--parallel", str(self.jobs)])
        self.built = True

    def stage(self, destination: Path, component: str) -> Path:
        self.build_native()
        destination.mkdir(parents=True, exist_ok=True)
        environment = self.environment.copy()
        environment["DESTDIR"] = str(destination)
        self.run([
            "cmake", "--install", str(self.build), "--component", component,
        ], env=environment)
        return destination

    def cpack(self, generator: str, work: Path, destination: Path) -> Path:
        self.build_native()
        work.mkdir(parents=True, exist_ok=True)
        environment = self.environment.copy()
        environment["SOURCE_DATE_EPOCH"] = str(self.epoch)
        self.run([
            "cpack", "--config", str(self.build / "CPackConfig.cmake"),
            "-G", generator, "-D", "CPACK_COMPONENTS_ALL=Unspecified", "-B", str(work),
        ], env=environment)
        suffix = {"TGZ": ".tar.gz", "DEB": ".deb", "RPM": ".rpm"}[generator]
        candidates = sorted(path for path in work.glob(f"*{suffix}") if path.is_file())
        if len(candidates) != 1:
            raise RuntimeError(f"expected one {generator} base package, found {len(candidates)}")
        shutil.move(candidates[0], destination)
        return destination
