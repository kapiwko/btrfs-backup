#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import time

from session import DesktopSession, run


SCRIPT_DIR = Path(__file__).resolve().parent
NOTIFICATION_PANEL_SCRIPT = (SCRIPT_DIR / "notification_panel.js").read_text()
WIDGET_PANEL_SCRIPT = (SCRIPT_DIR / "widget_panel.js").read_text()


def qdbus_script(session: DesktopSession, script: str) -> str:
    for _ in range(30):
        try:
            result = run(["qdbus6", "org.kde.plasmashell", "/PlasmaShell",
                          "org.kde.PlasmaShell.evaluateScript", script], env=session.environment,
                         capture=True, check=False, timeout=3)
        except subprocess.TimeoutExpired:
            time.sleep(1)
            continue
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
        time.sleep(1)
    raise RuntimeError("PlasmaShell scripting interface did not become available")


def invoke_widget(session: DesktopSession, widget_id: str) -> None:
    run(["qdbus6", "org.kde.kglobalaccel", "/component/plasmashell",
         "org.kde.kglobalaccel.Component.invokeShortcut", f"activate widget {widget_id}"],
        env=session.environment, timeout=3)


def active_window(session: DesktopSession, output: Path, build_dir: Path, source_dir: Path,
                  mode: str, page: str, scene: str, qml: str, delay: float) -> None:
    unused = mode, page, source_dir
    session.start([qml, "--apptype", "widget", "-I", str(build_dir),
                   "-I", str(build_dir / "integrations/kde"), "-f", scene], extra_env={
                       "LANG": "C.UTF-8", "KDE_FULL_SESSION": "true", "QT_STYLE_OVERRIDE": "Breeze",
                       "QT_QUICK_CONTROLS_STYLE": "org.kde.desktop"})
    time.sleep(delay)
    full = session.runtime_dir / "window-full.png"
    session.capture_root(full)
    run(["magick", str(full), "-trim", "+repage", str(output)], env=session.environment)


def notification(session: DesktopSession, output: Path, build_dir: Path, source_dir: Path,
                 mode: str, page: str, scene: str, qml: str, delay: float) -> None:
    unused = page, scene, qml, delay, source_dir
    session.environment.update({"QT_QUICK_BACKEND": "software",
                                "XDG_CONFIG_HOME": str(session.runtime_dir / "config")})
    Path(session.environment["XDG_CONFIG_HOME"]).mkdir()
    session.update_activation_environment("WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "XDG_RUNTIME_DIR",
                                          "XDG_DATA_HOME", "XDG_CONFIG_HOME")
    manager = build_dir / "integrations/kde/btrfs-backup-kde-manager-demo"
    demo = build_dir / "integrations/kde/btrfs-backup-kde-screenshot-demo"
    session.start([str(manager), "connected"])
    session.start(["plasmashell"])
    session.wait_for_bus_name("org.kde.JobViewServer" if mode == "transfer" else "org.freedesktop.Notifications")
    widget_id = qdbus_script(session, NOTIFICATION_PANEL_SCRIPT)
    time.sleep(2)
    session.start([str(demo), mode])
    time.sleep(2)
    invoke_widget(session, widget_id)
    time.sleep(2)
    full = session.runtime_dir / "notification-full.png"
    session.capture_root(full)
    run(["magick", str(full), "-crop", "420x170+604+100", "+repage", str(output)], env=session.environment)


def dolphin(session: DesktopSession, output: Path, build_dir: Path, source_dir: Path,
            mode: str, page: str, scene: str, qml: str, delay: float) -> None:
    unused = mode, page, scene, qml, delay, source_dir
    data = session.runtime_dir / "data"
    config = session.runtime_dir / "config"
    data.mkdir()
    config.mkdir()
    session.environment.update({"QT_STYLE_OVERRIDE": "Breeze", "XDG_DATA_HOME": str(data),
                                "XDG_CONFIG_HOME": str(config)})
    session.update_activation_environment("WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "XDG_RUNTIME_DIR",
                                          "XDG_DATA_HOME", "XDG_CONFIG_HOME")
    manager = build_dir / "integrations/kde/btrfs-backup-kde-manager-demo"
    session.start([str(manager), "connected"])
    session.wait_for_bus_name("io.github.btrfsbackup.Manager1", 5)
    session.start(["dolphin", "--new-window", "btrfsbackup:/home/home-2026-09-02T230854Z/kamil/Documents"],
                  extra_env={"DBUS_SYSTEM_BUS_ADDRESS": session.environment["DBUS_SESSION_BUS_ADDRESS"],
                             "QT_PLUGIN_PATH": str(build_dir / "bin"), "XDG_DATA_DIRS": f"{data}:/usr/share"})
    time.sleep(5)
    run(["xdotool", "key", "F9"], env=session.environment)
    time.sleep(1)
    run(["xdotool", "key", "alt+F10"], env=session.environment)
    time.sleep(3)
    full = session.runtime_dir / "dolphin-full.png"
    session.capture_root(full)
    run(["magick", str(full), "-trim", "+repage", str(output)], env=session.environment)


def plasma_widget(session: DesktopSession, output: Path, build_dir: Path, source_dir: Path,
                  mode: str, page: str, scene: str, qml: str, delay: float) -> None:
    unused = page, scene, qml, delay
    data = session.runtime_dir / "data"
    config = session.runtime_dir / "config"
    package = data / "plasma/plasmoids/org.btrfsbackup.plasmoid"
    package.parent.mkdir(parents=True)
    config.mkdir()
    shutil.copytree(source_dir / "integrations/kde/plasmoid/package", package)
    shutil.copy2(build_dir / "integrations/kde/metadata.json", package / "metadata.json")
    (package / "metadata.json.in").unlink(missing_ok=True)
    main_qml = package / "contents/ui/main.qml"
    contents = main_qml.read_text()
    contents = contents.replace("fullRepresentation: PlasmaExtras.Representation {\n",
                                "fullRepresentation: PlasmaExtras.Representation {\n"
                                "        header: PlasmaExtras.BasicPlasmoidHeading {}\n", 1)
    main_qml.write_text(contents)
    session.environment.update({"QT_QUICK_BACKEND": "software", "XDG_DATA_HOME": str(data),
                                "XDG_CONFIG_HOME": str(config)})
    session.update_activation_environment("WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "XDG_RUNTIME_DIR",
                                          "XDG_DATA_HOME", "XDG_CONFIG_HOME")
    manager = build_dir / "integrations/kde/btrfs-backup-kde-manager-demo"
    session.start([str(manager), mode])
    session.wait_for_bus_name("io.github.btrfsbackup.Manager1", 5)
    plasma = ["plasmashell"]
    if os.environ.get("BTRFS_BACKUP_SCREENSHOT_DEBUG") == "1":
        plasma = ["gdb", "-batch", "-ex", "run", "-ex", "bt", "--args", *plasma]
    session.start(plasma, extra_env={"DBUS_SYSTEM_BUS_ADDRESS": session.environment["DBUS_SESSION_BUS_ADDRESS"],
                                     "QML2_IMPORT_PATH": str(build_dir)})
    widget_id = qdbus_script(session, WIDGET_PANEL_SCRIPT)
    time.sleep(4)
    background = session.runtime_dir / "plasma-background.png"
    full = session.runtime_dir / "plasma-full.png"
    session.capture_root(background)
    invoke_widget(session, widget_id)
    time.sleep(4)
    session.capture_root(full)
    run(["magick", str(background), "(", str(full), "-crop", "440x441+584+283", "+repage", ")",
         "-geometry", "+554+283", "-composite", "(", str(full), "-crop", "1024x44+0+724", "+repage", ")",
         "-geometry", "+0+724", "-composite", "-crop", "520x520+504+248", "+repage", str(output)],
        env=session.environment)


def system_settings(session: DesktopSession, output: Path, build_dir: Path, source_dir: Path,
                    mode: str, page: str, scene: str, qml: str, delay: float) -> None:
    unused = scene, qml, delay
    data = session.runtime_dir / "data"
    applications = data / "applications"
    applications.mkdir(parents=True)
    session.environment.update({"QT_QUICK_BACKEND": "software", "QT_LINUX_ACCESSIBILITY_ALWAYS_ON": "1",
                                "QT_ACCESSIBILITY": "1", "NO_AT_BRIDGE": "0", "XDG_DATA_HOME": str(data)})
    session.update_activation_environment("WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "XDG_RUNTIME_DIR",
                                          "XDG_DATA_HOME", "QT_LINUX_ACCESSIBILITY_ALWAYS_ON",
                                          "QT_ACCESSIBILITY", "NO_AT_BRIDGE")
    manager = build_dir / "integrations/kde/btrfs-backup-kde-manager-demo"
    session.start([str(manager), mode])
    session.wait_for_bus_name("io.github.btrfsbackup.Manager1", 5)
    shutil.copy2(build_dir / "integrations/kde/kcm/kcm_btrfsbackup.desktop", applications)
    session.start(["systemsettings", "kcm_btrfsbackup"], extra_env={
        "DBUS_SYSTEM_BUS_ADDRESS": session.environment["DBUS_SESSION_BUS_ADDRESS"],
        "BTRFS_BACKUP_SCREENSHOT_PAGE": page, "XDG_DATA_DIRS": f"{data}:/usr/share",
        "QML2_IMPORT_PATH": str(build_dir), "QT_PLUGIN_PATH": str(build_dir / "bin")})
    run([str(source_dir / "tools/screenshots/kcm-navigate.py"), page or "wait"], env=session.environment)
    time.sleep(2)
    full = session.runtime_dir / "system-settings-full.png"
    session.capture_root(full)
    run(["magick", str(full), "-trim", "+repage", str(output)], env=session.environment)


SCENARIOS = {
    "active-window": active_window,
    "notification": notification,
    "dolphin": dolphin,
    "plasma-widget": plasma_widget,
    "system-settings": system_settings,
}
