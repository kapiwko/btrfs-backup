#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

import glob
import os
from pathlib import Path
import shutil
import subprocess
import sys


def required_program(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise RuntimeError(f"{name} is required")
    return path


def clang_tidy_diff_path() -> str:
    configured = os.environ.get("CLANG_TIDY_DIFF")
    if configured:
        return configured
    installed = shutil.which("clang-tidy-diff.py")
    if installed:
        return installed
    candidates = ["/usr/share/clang/clang-tidy-diff.py"]
    candidates.extend(glob.glob("/usr/lib/llvm*/share/clang/clang-tidy-diff.py"))
    for candidate in candidates:
        if Path(candidate).is_file():
            return candidate
    raise RuntimeError("clang-tidy-diff.py is required")


def run() -> int:
    root = Path(__file__).resolve().parent.parent
    build_dir = Path(os.environ.get("CLANG_TIDY_BUILD_DIR", root / "build/clang-tidy-changed"))
    base_ref = os.environ.get("CLANG_TIDY_BASE", "HEAD")
    clang_tidy = os.environ.get("CLANG_TIDY", "clang-tidy")
    jobs = os.environ.get("BUILD_JOBS", str(os.cpu_count() or 2))

    cmake = required_program("cmake")
    required_program("clang++")
    git = required_program("git")
    required_program(clang_tidy)
    tidy_diff = clang_tidy_diff_path()

    revision = subprocess.run(
        [git, "-C", str(root), "rev-parse", "--verify", "--quiet", f"{base_ref}^{{commit}}"],
        check=False,
        stdout=subprocess.DEVNULL,
    )
    if revision.returncode != 0:
        raise RuntimeError(f"clang-tidy base is not a commit: {base_ref}")

    subprocess.run(
        [
            cmake,
            "-S",
            str(root),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DCMAKE_CXX_COMPILER=clang++",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            "-DBUILD_KDE_INTEGRATION=OFF",
            "-DBUILD_TESTING=OFF",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    diff = subprocess.run(
        [
            git,
            "-C",
            str(root),
            "diff",
            "--no-ext-diff",
            "--unified=0",
            base_ref,
            "--",
            "apps",
            "src",
        ],
        check=True,
        stdout=subprocess.PIPE,
    )
    return subprocess.run(
        [
            sys.executable,
            tidy_diff,
            "-clang-tidy-binary",
            clang_tidy,
            "-p",
            "1",
            "-regex",
            r"^(apps|src)/.*\.(c|cc|cpp|cxx)$",
            "-path",
            str(build_dir),
            "-config-file",
            str(root / ".clang-tidy"),
            "-j",
            jobs,
            "-quiet",
            "-only-check-in-db",
        ],
        input=diff.stdout,
        check=False,
    ).returncode


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2) from error
