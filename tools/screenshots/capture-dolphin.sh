#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

output="${BTRFS_BACKUP_SCREENSHOT_OUTPUT:?}"
build_dir="${BTRFS_BACKUP_SCREENSHOT_BUILD_DIR:?}"
runtime_dir="${XDG_RUNTIME_DIR:?}"
data_home="$runtime_dir/data"
config_home="$runtime_dir/config"
manager="$build_dir/integrations/kde/btrfs-backup-kde-manager-demo"
full_capture="$runtime_dir/dolphin-full.png"

export QT_QPA_PLATFORM=wayland
export QT_STYLE_OVERRIDE=Breeze
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland
export XDG_DATA_HOME="$data_home"
export XDG_CONFIG_HOME="$config_home"

cleanup() {
    set +e
    [[ -n "${dolphin_pid:-}" ]] && kill "$dolphin_pid" 2>/dev/null
    [[ -n "${manager_pid:-}" ]] && kill "$manager_pid" 2>/dev/null
    [[ -n "${dolphin_pid:-}" ]] && wait "$dolphin_pid" 2>/dev/null
    [[ -n "${manager_pid:-}" ]] && wait "$manager_pid" 2>/dev/null
}
trap cleanup EXIT

mkdir -p "$data_home" "$config_home"
dbus-update-activation-environment \
    WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_RUNTIME_DIR XDG_DATA_HOME XDG_CONFIG_HOME

"$manager" connected &
manager_pid=$!
for _ in {1..100}; do
    busctl --user --timeout=1 list 2>/dev/null \
        | grep -Fq 'io.github.btrfsbackup.Manager1' && break
    sleep 0.05
done

DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" \
QT_PLUGIN_PATH="$build_dir/bin" \
XDG_DATA_DIRS="$data_home:/usr/share" \
    dolphin --new-window \
        'btrfsbackup:/home/home-2026-09-02T230854Z/kamil/Documents' &
dolphin_pid=$!
sleep 5
xdotool key F9
sleep 1
xdotool key alt+F10
sleep 3

import -display "$DISPLAY" -window root "$full_capture"
magick "$full_capture" -trim +repage "$output"
