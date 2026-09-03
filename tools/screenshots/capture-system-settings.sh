#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

mode="${BTRFS_BACKUP_SCREENSHOT_MODE:?}"
page="${BTRFS_BACKUP_SCREENSHOT_PAGE:-}"
output="${BTRFS_BACKUP_SCREENSHOT_OUTPUT:?}"
build_dir="${BTRFS_BACKUP_SCREENSHOT_BUILD_DIR:?}"
runtime_dir="${XDG_RUNTIME_DIR:?}"
data_home="$runtime_dir/data"
manager="$build_dir/integrations/kde/btrfs-backup-kde-manager-demo"

export QT_QPA_PLATFORM=wayland
export QT_QUICK_BACKEND=software
export QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
export QT_ACCESSIBILITY=1
export NO_AT_BRIDGE=0
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland

cleanup() {
    set +e
    [[ -n "${settings_pid:-}" ]] && kill "$settings_pid" 2>/dev/null
    [[ -n "${manager_pid:-}" ]] && kill "$manager_pid" 2>/dev/null
    [[ -n "${settings_pid:-}" ]] && wait "$settings_pid" 2>/dev/null
    [[ -n "${manager_pid:-}" ]] && wait "$manager_pid" 2>/dev/null
}
trap cleanup EXIT

mkdir -p "$data_home/applications"
export XDG_DATA_HOME="$data_home"
dbus-update-activation-environment \
    WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_RUNTIME_DIR XDG_DATA_HOME \
    QT_LINUX_ACCESSIBILITY_ALWAYS_ON QT_ACCESSIBILITY NO_AT_BRIDGE
"$manager" "$mode" &
manager_pid=$!
for _ in {1..100}; do
    busctl --user --timeout=1 list 2>/dev/null \
        | grep -Fq 'io.github.btrfsbackup.Manager1' && break
    sleep 0.05
done

cp "$build_dir/integrations/kde/kcm/kcm_btrfsbackup.desktop" "$data_home/applications/"
DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" \
BTRFS_BACKUP_SCREENSHOT_PAGE="$page" \
XDG_DATA_DIRS="$data_home:/usr/share" \
QML2_IMPORT_PATH="$build_dir" \
QT_PLUGIN_PATH="$build_dir/bin" \
    systemsettings kcm_btrfsbackup &
settings_pid=$!
"$BTRFS_BACKUP_SCREENSHOT_SOURCE_DIR/tools/screenshots/kcm-navigate.py" "${page:-wait}"
sleep 2

full_capture="$runtime_dir/system-settings-full.png"
import -display "$DISPLAY" -window root "$full_capture"
magick "$full_capture" -trim +repage "$output"
