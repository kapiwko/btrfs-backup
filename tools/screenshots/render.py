#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from scenarios import SCENARIOS
from session import DesktopSession, run


ROOT = Path(__file__).resolve().parents[2]
TARGETS = (
    "btrfsbackup_kde_modelsplugin", "btrfs-backup-kde-manager-demo", "kio_btrfsbackup",
    "kcm_btrfsbackup", "btrfs-backup-kde-restore", "btrfs-backup-kde-screenshot-demo",
)
SCREENSHOT_TIME = "2026-09-03 12:14:00"
FAKETIME_LIBRARY_PATHS = (
    Path("/usr/lib/faketime/libfaketimeMT.so.1"),
    Path("/usr/lib/faketime/libfaketime.so.1"),
    Path("/usr/lib/x86_64-linux-gnu/faketime/libfaketimeMT.so.1"),
    Path("/usr/lib/x86_64-linux-gnu/faketime/libfaketime.so.1"),
)


def scenario_specs(only: str, kcm_page: str) -> list[dict[str, str]]:
    specs: list[dict[str, str]] = []
    if only in ("all", "plasma"):
        specs += [{"kind": "plasma-widget", "mode": mode, "output": f"plasma-widget-{mode}.png"}
                  for mode in ("connected", "disconnected", "transferring")]
    if only in ("all", "notifications"):
        specs += [
            {"kind": "notification", "mode": "transfer", "output": "kui-job-transferring.png"},
            {"kind": "notification", "mode": "completion", "output": "notification-completed.png"},
        ]
    if only in ("all", "restore"):
        specs.append({"kind": "active-window", "scene": str(ROOT / "tools/screenshots/restore-dialog.qml"),
                      "output": "restore-dialog.png"})
    if only in ("all", "dolphin"):
        specs.append({"kind": "dolphin", "output": "dolphin-browse.png"})
    if only in ("all", "kcm"):
        pages = {
            "overview": (("connected", "system-settings-connected.png"),
                         ("disconnected", "system-settings-disconnected.png"),
                         ("transferring", "system-settings-transferring.png")),
            "profile-details": (("connected", "system-settings-profile-details.png"),),
            "profile-settings": (("connected", "system-settings-profile-settings.png"),),
            "history": (("connected", "system-settings-history.png"),),
            "new-profile": (("connected", "system-settings-new-profile.png"),),
            "new-profile-adopt": (("connected", "system-settings-new-profile-adopt.png"),),
            "new-profile-adopt-partition": (("connected", "system-settings-new-profile-adopt-partition.png"),),
            "new-profile-prepare": (("connected", "system-settings-new-profile-prepare.png"),),
            "new-profile-prepare-partition": (("connected", "system-settings-new-profile-prepare-partition.png"),),
        }
        for page, variants in pages.items():
            if kcm_page not in ("all", page):
                continue
            specs += [{"kind": "system-settings", "mode": mode,
                       "page": "" if page == "overview" else page, "output": output}
                      for mode, output in variants]
    return specs


def find_qml() -> str:
    configured = os.environ.get("QML_EXECUTABLE")
    executable = configured or shutil.which("qml6") or shutil.which("qml")
    if not executable:
        raise RuntimeError("qml6 or qml is required to render screenshots")
    return executable


def session_identity(output_name: str) -> str:
    return hashlib.sha256(output_name.encode()).hexdigest()[:12]


def deterministic_process_environment() -> dict[str, str]:
    configured = os.environ.get("BTRFS_BACKUP_SCREENSHOT_FAKETIME_LIBRARY")
    candidates = (Path(configured),) if configured else FAKETIME_LIBRARY_PATHS
    library = next((path for path in candidates if path.is_file()), None)
    if library is None:
        raise RuntimeError("libfaketime is required to render deterministic screenshots")
    preload = str(library)
    if existing := os.environ.get("LD_PRELOAD"):
        preload += f":{existing}"
    return {
        "FAKETIME": SCREENSHOT_TIME,
        "FAKETIME_DONT_FAKE_MONOTONIC": "1",
        "LD_PRELOAD": preload,
        "TZ": "UTC",
    }


def normalize_png(output: Path) -> None:
    normalized = output.with_name(f".{output.name}.normalized")
    try:
        run(["magick", str(output), "-strip", "-define", "png:exclude-chunks=date,time", str(normalized)])
        normalized.replace(output)
    finally:
        normalized.unlink(missing_ok=True)


def build(build_dir: Path) -> None:
    run(["cmake", "-S", str(ROOT), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release",
         "-DBUILD_KDE_INTEGRATION=ON", "-DBUILD_README_SCREENSHOTS=ON", "-DBUILD_TESTING=OFF"])
    run(["cmake", "--build", str(build_dir), "--target", *TARGETS, "--parallel"])


def capture(spec: dict[str, str], output_dir: Path, build_dir: Path, qml: str) -> dict[str, object]:
    name = Path(spec["output"]).stem
    identity = session_identity(name)
    runtime_dir = Path(f"/tmp/btrfs-backup-readme-{os.getuid()}/{identity}")
    shutil.rmtree(runtime_dir, ignore_errors=True)
    runtime_dir.mkdir(parents=True, mode=0o700)
    output = output_dir / spec["output"]
    output.unlink(missing_ok=True)
    payload = {"scenario": spec["kind"], "output": str(output), "build_dir": str(build_dir),
               "source_dir": str(ROOT), "qml": qml, "mode": spec.get("mode", ""),
               "page": spec.get("page", ""), "scene": spec.get("scene", ""), "delay": 1.0}
    arguments = [str(Path(__file__).resolve())]
    environment = {**os.environ, **deterministic_process_environment(), "XDG_RUNTIME_DIR": str(runtime_dir),
                   "BTRFS_BACKUP_SCREENSHOT_CAPTURE": json.dumps(payload)}
    if spec["kind"] == "notification":
        data = build_dir / "screenshot-data"
        notify = data / "knotifications6/btrfs-backup-kde-monitor.notifyrc"
        notify.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "integrations/kde/monitor/btrfs-backup-kde-monitor.notifyrc", notify)
        environment["XDG_DATA_HOME"] = str(data)
    run(["dbus-run-session", "--", "kwin_wayland", "--x11-display", environment["DISPLAY"],
         "--fullscreen", "1", "--socket", f"wayland-bb-{identity}", "--width", "1024", "--height", "768",
         "--no-lockscreen", "--no-global-shortcuts", "--exit-with-session", *arguments], env=environment)
    if not output.is_file():
        raise RuntimeError(f"expected screenshot was not created: {output}")
    normalize_png(output)
    print(f"Rendered {output}")
    return {"scenario": spec["kind"], "mode": spec.get("mode", ""), "page": spec.get("page", ""),
            "path": output.name, "bytes": output.stat().st_size,
            "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}


def render(output_dir: Path, only: str, kcm_page: str) -> None:
    build_dir = Path(os.environ.get("BTRFS_BACKUP_SCREENSHOT_BUILD_DIR",
                                    ROOT / "build/readme-screenshots")).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    output_dir = output_dir.resolve()
    qml = find_qml()
    build(build_dir)
    environment = {**os.environ, "DISPLAY": ":99"}
    xvfb = subprocess.Popen(["Xvfb", environment["DISPLAY"], "-screen", "0", "1024x768x24", "-nolisten", "tcp"],
                            env=environment)
    old_display = os.environ.get("DISPLAY")
    os.environ["DISPLAY"] = environment["DISPLAY"]
    try:
        time.sleep(1)
        results: list[dict[str, object]] = []
        manifest: dict[str, object] = {"schema": 1, "complete": False, "screenshots": results}
        for spec in scenario_specs(only, kcm_page):
            results.append(capture(spec, output_dir, build_dir, qml))
            (output_dir / "screenshot-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        manifest["complete"] = True
        (output_dir / "screenshot-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    finally:
        if old_display is None:
            os.environ.pop("DISPLAY", None)
        else:
            os.environ["DISPLAY"] = old_display
        xvfb.terminate()
        xvfb.wait()


def container(output_dir: str, only: str, kcm_page: str) -> None:
    image = os.environ.get("BTRFS_BACKUP_SCREENSHOT_IMAGE", "btrfs-backup-screenshots:local")
    run(["docker", "build", "--tag", image, "--file", str(ROOT / "tools/screenshots/Dockerfile"),
         str(ROOT / "tools/screenshots")])
    command = ["docker", "run", "--rm", "--cap-add", "SYS_NICE", "--user", f"{os.getuid()}:{os.getgid()}",
               "--env", "HOME=/tmp", "--env", "BTRFS_BACKUP_SCREENSHOT_BUILD_DIR=/workspace/build/readme-screenshots-container"]
    for name in ("BTRFS_BACKUP_SCREENSHOT_DEBUG", "BTRFS_BACKUP_SCREENSHOT_KCM_PAGE",
                 "BTRFS_BACKUP_SCREENSHOT_ONLY"):
        command += ["--env", name]
    command += ["--volume", "/etc/passwd:/etc/passwd:ro", "--volume", "/etc/group:/etc/group:ro",
                "--volume", f"{ROOT}:/workspace", "--workdir", "/workspace", image, "python3",
                "tools/screenshots/render.py", "render", output_dir]
    command += ["--only", only, "--kcm-page", kcm_page]
    run(command)


def main() -> int:
    capture_payload = os.environ.get("BTRFS_BACKUP_SCREENSHOT_CAPTURE")
    if capture_payload and len(sys.argv) == 1:
        payload = json.loads(capture_payload)
        with DesktopSession(Path(os.environ["XDG_RUNTIME_DIR"])) as session:
            SCENARIOS[payload["scenario"]](session, Path(payload["output"]), Path(payload["build_dir"]),
                                           Path(payload["source_dir"]), payload["mode"], payload["page"],
                                           payload["scene"], payload["qml"], payload["delay"])
        return 0
    parser = argparse.ArgumentParser(description="Render deterministic project screenshots.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    render_parser = subparsers.add_parser("render", help="render screenshots in the current environment")
    render_parser.add_argument("output", nargs="?", default=str(ROOT / "docs/images"))
    render_parser.add_argument("--only", choices=("all", "plasma", "notifications", "restore", "dolphin", "kcm"),
                               default=os.environ.get("BTRFS_BACKUP_SCREENSHOT_ONLY", "all"))
    render_parser.add_argument("--kcm-page", default=os.environ.get("BTRFS_BACKUP_SCREENSHOT_KCM_PAGE", "all"))
    container_parser = subparsers.add_parser("container", help="build an isolated image and render screenshots")
    container_parser.add_argument("output", nargs="?", default="docs/images")
    container_parser.add_argument("--only", choices=("all", "plasma", "notifications", "restore", "dolphin", "kcm"),
                                  default=os.environ.get("BTRFS_BACKUP_SCREENSHOT_ONLY", "all"))
    container_parser.add_argument("--kcm-page", default=os.environ.get("BTRFS_BACKUP_SCREENSHOT_KCM_PAGE", "all"))
    args = parser.parse_args()
    if args.command == "render":
        render(Path(args.output), args.only, args.kcm_page)
    else:
        container(args.output, args.only, args.kcm_page)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
