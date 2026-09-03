#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

scene="${BTRFS_BACKUP_SCREENSHOT_SCENE:?}"
output="${BTRFS_BACKUP_SCREENSHOT_OUTPUT:?}"
qml_executable="${BTRFS_BACKUP_SCREENSHOT_QML:?}"
build_dir="${BTRFS_BACKUP_SCREENSHOT_BUILD_DIR:?}"
delay="${BTRFS_BACKUP_SCREENSHOT_DELAY:-1}"

cd "$(dirname "$output")"

LANG=C.UTF-8 \
KDE_FULL_SESSION=true \
QT_QPA_PLATFORM=wayland \
QT_STYLE_OVERRIDE=Breeze \
QT_QUICK_CONTROLS_STYLE=org.kde.desktop \
    "$qml_executable" \
    --apptype widget \
    -I "$build_dir" \
    -I "$build_dir/integrations/kde" \
    -f "$scene" &
qml_pid=$!
sleep "$delay"

full_capture="${XDG_RUNTIME_DIR:?}/window-full.png"
import -display "$DISPLAY" -window root "$full_capture"
magick "$full_capture" -trim +repage "$output"

kill "$qml_pid" 2>/dev/null || true
wait "$qml_pid" 2>/dev/null || true
