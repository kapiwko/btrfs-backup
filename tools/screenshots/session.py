#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import time


def run(arguments: list[str], *, env: dict[str, str] | None = None, capture: bool = False,
        check: bool = True, timeout: float | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, check=check, text=True, capture_output=capture, env=env, timeout=timeout)


class DesktopSession:
    def __init__(self, runtime_dir: Path) -> None:
        self.runtime_dir = runtime_dir
        self.processes: list[subprocess.Popen[bytes]] = []
        self.environment = {
            **os.environ,
            "QT_QPA_PLATFORM": "wayland",
            "XDG_CURRENT_DESKTOP": "KDE",
            "XDG_SESSION_DESKTOP": "KDE",
            "XDG_SESSION_TYPE": "wayland",
        }

    def __enter__(self) -> DesktopSession:
        self.runtime_dir.mkdir(parents=True, mode=0o700, exist_ok=True)
        return self

    def __exit__(self, unused_type: object, unused_value: object, unused_traceback: object) -> None:
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
        for process in reversed(self.processes):
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    def start(self, arguments: list[str], *, extra_env: dict[str, str] | None = None) -> subprocess.Popen[bytes]:
        environment = {**self.environment, **(extra_env or {})}
        process = subprocess.Popen(arguments, env=environment)
        self.processes.append(process)
        return process

    def update_activation_environment(self, *names: str) -> None:
        run(["dbus-update-activation-environment", *names], env=self.environment)

    def wait_for_bus_name(self, name: str, timeout: float = 15.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = run(["busctl", "--user", "--timeout=1", "list"], env=self.environment,
                         capture=True, check=False)
            if result.returncode == 0 and name in result.stdout:
                return
            time.sleep(0.1)
        raise RuntimeError(f"D-Bus service did not become available: {name}")

    def capture_root(self, destination: Path) -> None:
        run(["import", "-display", self.environment["DISPLAY"], "-window", "root", str(destination)],
            env=self.environment)
