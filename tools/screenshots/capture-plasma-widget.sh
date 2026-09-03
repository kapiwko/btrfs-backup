#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

mode="${BTRFS_BACKUP_SCREENSHOT_MODE:?}"
output="${BTRFS_BACKUP_SCREENSHOT_OUTPUT:?}"
build_dir="${BTRFS_BACKUP_SCREENSHOT_BUILD_DIR:?}"
source_dir="${BTRFS_BACKUP_SCREENSHOT_SOURCE_DIR:?}"
runtime_dir="${XDG_RUNTIME_DIR:?}"
data_home="$runtime_dir/data"
config_home="$runtime_dir/config"
manager="$build_dir/integrations/kde/btrfs-backup-kde-manager-demo"
full_capture="$runtime_dir/plasma-full.png"
background_capture="$runtime_dir/plasma-background.png"

export QT_QPA_PLATFORM=wayland
export QT_QUICK_BACKEND=software
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland

cleanup() {
    set +e
    [[ -n "${plasma_pid:-}" ]] && kill "$plasma_pid" 2>/dev/null
    [[ -n "${manager_pid:-}" ]] && kill "$manager_pid" 2>/dev/null
    [[ -n "${plasma_pid:-}" ]] && wait "$plasma_pid" 2>/dev/null
    [[ -n "${manager_pid:-}" ]] && wait "$manager_pid" 2>/dev/null
}
trap cleanup EXIT

mkdir -p "$data_home/plasma/plasmoids" "$config_home"
export XDG_DATA_HOME="$data_home"
export XDG_CONFIG_HOME="$config_home"
dbus-update-activation-environment \
    WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_RUNTIME_DIR XDG_DATA_HOME XDG_CONFIG_HOME
cp -a "$source_dir/integrations/kde/plasmoid/package" \
    "$data_home/plasma/plasmoids/org.btrfsbackup.plasmoid"
cp "$build_dir/integrations/kde/metadata.json" \
    "$data_home/plasma/plasmoids/org.btrfsbackup.plasmoid/metadata.json"
rm -f "$data_home/plasma/plasmoids/org.btrfsbackup.plasmoid/metadata.json.in"
# The real System Tray supplies this heading for embedded applets. The isolated
# capture places the applet directly in its temporary panel, so add the same
# standard Plasma heading to the disposable package copy used by the renderer.
perl -0pi -e \
    's/fullRepresentation: PlasmaExtras\.Representation \{\n/fullRepresentation: PlasmaExtras.Representation {\n        header: PlasmaExtras.BasicPlasmoidHeading {}\n/' \
    "$data_home/plasma/plasmoids/org.btrfsbackup.plasmoid/contents/ui/main.qml"

"$manager" "$mode" &
manager_pid=$!
for _ in {1..100}; do
    busctl --user --timeout=1 list 2>/dev/null \
        | grep -Fq 'io.github.btrfsbackup.Manager1' && break
    sleep 0.05
done

if [[ "${BTRFS_BACKUP_SCREENSHOT_DEBUG:-0}" == "1" ]]; then
    DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" \
    QML2_IMPORT_PATH="$build_dir" \
        gdb -batch -ex run -ex bt --args plasmashell &
else
    DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" \
    QML2_IMPORT_PATH="$build_dir" \
        plasmashell &
fi
plasma_pid=$!
widget_id=""
for _ in {1..30}; do
    widget_id="$(timeout 3 qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript \
        'var oldPanels = panels(); for (var i = 0; i < oldPanels.length; ++i) oldPanels[i].remove(); var panel = new Panel; panel.location = "bottom"; panel.alignment = "center"; panel.length = 620; panel.height = 44; panel.floating = false; panel.addWidget("org.kde.plasma.panelspacer"); var widget = panel.addWidget("org.btrfsbackup.plasmoid"); widget.currentConfigGroup = ["General"]; widget.writeConfig("showStorage", false); widget.globalShortcut = "Meta+B"; panel.addWidget("org.kde.plasma.digitalclock"); print(widget.id);' \
        2>/dev/null || true)"
    [[ -n "$widget_id" ]] && break
    sleep 1
done
if [[ -z "$widget_id" ]]; then
    printf 'PlasmaShell scripting interface did not become available\n' >&2
    timeout 3 qdbus6 org.kde.plasmashell /PlasmaShell >&2 || true
    if ! kill -0 "$plasma_pid" 2>/dev/null; then
        wait "$plasma_pid" || printf 'plasmashell exited with status %s\n' "$?" >&2
        plasma_pid=""
    fi
    exit 2
fi
sleep 4
import -display "$DISPLAY" -window root "$background_capture"
timeout 3 qdbus6 org.kde.kglobalaccel "/component/plasmashell" \
    org.kde.kglobalaccel.Component.invokeShortcut "activate widget $widget_id"
sleep 4

import -display "$DISPLAY" -window root "$full_capture"
magick "$background_capture" \
    \( "$full_capture" -crop 440x441+584+283 +repage \) -geometry +554+283 -composite \
    \( "$full_capture" -crop 1024x44+0+724 +repage \) -geometry +0+724 -composite \
    -crop 520x520+504+248 +repage "$output"
