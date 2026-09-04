#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import zipfile


SOURCE_DIRECTORIES = ("LICENSES", "apps", "cmake", "data", "docs", "integrations", "packaging",
                      "src", "tests", "tools")
SOURCE_FILES = ("VERSION", "README.md", "CHANGELOG.md", "TODO.md", "LICENSE", "REUSE.toml",
                ".gitignore", "CMakeLists.txt", "CMakePresets.json", "Makefile")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def source_date_epoch(root: Path) -> int:
    configured = os.environ.get("SOURCE_DATE_EPOCH")
    if configured is not None:
        if not configured.isdigit() or int(configured) <= 0:
            raise ValueError("SOURCE_DATE_EPOCH must be a positive Unix timestamp.")
        return int(configured)
    result = subprocess.run(["git", "-C", str(root), "log", "-1", "--format=%ct"],
                            text=True, capture_output=True, check=False)
    if result.returncode == 0 and result.stdout.strip().isdigit():
        return int(result.stdout.strip())
    return int((root / "VERSION").stat().st_mtime)


def reset_directory(path: Path) -> None:
    resolved = path.resolve()
    unsafe = {Path("/"), Path("/etc"), Path("/usr"), Path("/var"), Path("/home"), Path("/root")}
    if resolved in unsafe:
        raise ValueError(f"refusing unsafe dist directory: {resolved}")
    shutil.rmtree(resolved, ignore_errors=True)
    resolved.mkdir(parents=True, mode=0o755)


def copy_source_tree(root: Path, destination: Path, epoch: int) -> list[Path]:
    destination.mkdir(parents=True, mode=0o755)
    for name in SOURCE_DIRECTORIES:
        shutil.copytree(root / name, destination / name,
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"), symlinks=True)
    for name in SOURCE_FILES:
        shutil.copy2(root / name, destination / name)
    entries = sorted(destination.rglob("*"), key=lambda path: path.as_posix())
    for entry in [destination, *entries]:
        if entry.is_symlink():
            os.utime(entry, (epoch, epoch), follow_symlinks=False)
        else:
            entry.chmod(0o755 if entry.is_dir() or os.access(entry, os.X_OK) else 0o644)
            os.utime(entry, (epoch, epoch))
    return [path for path in entries if path.is_file() and not path.is_symlink()]


def deterministic_tar_gz(source: Path, destination: Path, epoch: int) -> None:
    with destination.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=epoch) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                paths = [source, *sorted(source.rglob("*"), key=lambda path: path.as_posix())]
                for path in paths:
                    relative = path.relative_to(source.parent).as_posix()
                    info = archive.gettarinfo(str(path), arcname=relative)
                    info.uid = 0
                    info.gid = 0
                    info.uname = "root"
                    info.gname = "root"
                    info.mtime = epoch
                    if info.isfile():
                        with path.open("rb") as contents:
                            archive.addfile(info, contents)
                    else:
                        archive.addfile(info)


def deterministic_zip(source: Path, destination: Path, epoch: int) -> None:
    timestamp = time.gmtime(max(epoch, 315532800))[:6]
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(source.rglob("*"), key=lambda item: item.as_posix()):
            if not path.is_file() or path.is_symlink():
                continue
            info = zipfile.ZipInfo(path.relative_to(source.parent).as_posix(), timestamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (path.stat().st_mode & 0xFFFF) << 16
            archive.writestr(info, path.read_bytes(), compresslevel=9)


def write_sbom(source: Path, destination: Path, version: str, epoch: int) -> None:
    files = []
    for index, path in enumerate(sorted(source.rglob("*"), key=lambda item: item.as_posix()), start=1):
        if path.is_file() and not path.is_symlink():
            files.append({"SPDXID": f"SPDXRef-File-{index}", "fileName": path.relative_to(source).as_posix(),
                          "checksums": [{"algorithm": "SHA256", "checksumValue": sha256(path)}]})
    document = {
        "spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0", "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"btrfs-backup-{version}",
        "documentNamespace": f"https://github.com/kapiwko/btrfs-backup/spdx/{version}/{epoch}",
        "creationInfo": {"created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(epoch)),
                         "creators": ["Tool: btrfs-backup-release.py"]},
        "files": files,
    }
    destination.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
