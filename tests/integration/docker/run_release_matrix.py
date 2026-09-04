#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import os
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
IMAGE = "btrfs-backup-release-builder"


def run(command: list[str]) -> None:
    print(f"+ {' '.join(command)}", flush=True)
    subprocess.run(command, check=True)


def checksums(path: Path) -> dict[str, str]:
    result = {}
    for line in path.read_text().splitlines():
        digest, name = line.split("  ", 1)
        result[name] = digest
    return result


def main() -> int:
    run(["docker", "build", "-t", IMAGE, "-f",
         str(ROOT / "tests/integration/docker/Dockerfile.release"), str(ROOT)])
    root = Path(tempfile.mkdtemp(prefix="btrfs-backup-release-matrix.", dir="/tmp"))
    try:
        cache = root / "ccache"
        cache.mkdir()
        for name in ("first", "second"):
            output = root / name
            build = root / f"build-{name}"
            output.mkdir()
            build.mkdir()
            run([
                "docker", "run", "--rm", "--network=none", "--user", f"{os.getuid()}:{os.getgid()}",
                "-e", "HOME=/tmp", "-e", "BUILD_JOBS=4", "-e", "CCACHE_DIR=/ccache",
                "-e", "SOURCE_DATE_EPOCH=1700000000", "-v", f"{ROOT}:/work:ro",
                "-v", f"{cache}:/ccache", "-v", f"{build}:/build",
                "-v", f"{output}:/artifacts", "-w", "/work", IMAGE,
                "/work/tools/release.py", "--target", "all", "--build-dir", "/build",
                "--skip-tests", "--dist-dir", "/artifacts",
            ])
            run(["docker", "run", "--rm", "--network=none", "-v", f"{output}:/artifacts:ro",
                 "-w", "/artifacts", IMAGE, "sha256sum", "-c", "SHA256SUMS"])
        first = checksums(root / "first/SHA256SUMS")
        second = checksums(root / "second/SHA256SUMS")
        different = sorted(name for name in first.keys() | second.keys()
                           if first.get(name) != second.get(name))
        if different:
            raise RuntimeError("release matrix is not reproducible for: " + ", ".join(different))
    except Exception:
        print(f"release matrix preserved at {root}", flush=True)
        raise
    else:
        shutil.rmtree(root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
