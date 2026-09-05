#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

from release_arch import build_arch_package  # noqa: E402


FILES = {
    "usr/share/locale/pl/LC_MESSAGES/alpha.mo": b"alpha\n",
    "usr/share/locale/pl/LC_MESSAGES/beta.mo": b"beta\n",
    "usr/share/plasma/plasmoids/example/contents/ui/main.qml": b"Item {}\n",
}


def prepare(stage: Path, paths: list[str]) -> None:
    for relative in paths:
        path = stage / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(FILES[relative])


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="btrfs-backup-arch-order.", dir="/tmp") as temporary:
        root = Path(temporary)
        first_stage = root / "first-stage"
        second_stage = root / "second-stage"
        first_stage.mkdir()
        second_stage.mkdir()
        prepare(first_stage, list(FILES))
        prepare(second_stage, list(reversed(FILES)))

        first = build_arch_package(ROOT, first_stage, root / "first.pkg.tar.zst", "4.0.0", "x86_64", 1700000000, kde=True)
        second = build_arch_package(ROOT, second_stage, root / "second.pkg.tar.zst", "4.0.0", "x86_64", 1700000000, kde=True)
        if first.read_bytes() != second.read_bytes():
            raise RuntimeError("Arch package depends on filesystem creation order")
    print("ok - Arch package member order is deterministic")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
