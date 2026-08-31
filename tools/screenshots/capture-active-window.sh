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
QT_QPA_PLATFORM=wayland \
QT_QUICK_CONTROLS_STYLE=org.kde.desktop \
    "$qml_executable" \
    -I "$build_dir" \
    -I "$build_dir/integrations/kde" \
    -f "$scene" &
qml_pid=$!
sleep "$delay"

spectacle --new-instance --activewindow --background --nonotify --no-shadow \
    --output "$output"

kill "$qml_pid" 2>/dev/null || true
wait "$qml_pid" 2>/dev/null || true
