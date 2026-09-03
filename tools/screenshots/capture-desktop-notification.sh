#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

mode="${BTRFS_BACKUP_SCREENSHOT_MODE:?}"
output="${BTRFS_BACKUP_SCREENSHOT_OUTPUT:?}"
demo="${BTRFS_BACKUP_SCREENSHOT_DEMO:?}"
build_dir="${BTRFS_BACKUP_SCREENSHOT_BUILD_DIR:?}"
runtime_dir="${XDG_RUNTIME_DIR:?}"
full_capture="$runtime_dir/notification-full.png"
manager="$build_dir/integrations/kde/btrfs-backup-kde-manager-demo"

export QT_QPA_PLATFORM=wayland
export QT_QUICK_BACKEND=software
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland

cleanup() {
    set +e
    [[ -n "${demo_pid:-}" ]] && kill "$demo_pid" 2>/dev/null
    [[ -n "${plasma_pid:-}" ]] && kill "$plasma_pid" 2>/dev/null
    [[ -n "${manager_pid:-}" ]] && kill "$manager_pid" 2>/dev/null
}
trap cleanup EXIT

mkdir -p "$runtime_dir/config"
export XDG_CONFIG_HOME="$runtime_dir/config"
dbus-update-activation-environment \
    WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_RUNTIME_DIR XDG_DATA_HOME XDG_CONFIG_HOME
"$manager" connected &
manager_pid=$!
plasmashell &
plasma_pid=$!
if [[ "$mode" == "transfer" ]]; then
    readiness_service="org.kde.JobViewServer"
else
    readiness_service="org.freedesktop.Notifications"
fi
service_ready=0
for _ in {1..150}; do
    if busctl --user --timeout=1 list 2>/dev/null \
        | grep -Fq "$readiness_service"; then
        service_ready=1
        break
    fi
    sleep 0.1
done
if [[ "$service_ready" != "1" ]]; then
    printf 'Plasma service did not become available: %s\n' "$readiness_service" >&2
    exit 2
fi
notification_widget_id=""
for _ in {1..30}; do
    notification_widget_id="$(timeout 3 qdbus6 org.kde.plasmashell /PlasmaShell \
        org.kde.PlasmaShell.evaluateScript \
        'var oldPanels = panels(); for (var i = 0; i < oldPanels.length; ++i) oldPanels[i].remove(); var panel = new Panel; panel.location = "bottom"; panel.alignment = "center"; panel.length = 1024; panel.height = 44; panel.floating = false; panel.addWidget("org.kde.plasma.panelspacer"); var widget = panel.addWidget("org.kde.plasma.notifications"); widget.globalShortcut = "Meta+N"; panel.addWidget("org.kde.plasma.digitalclock"); print(widget.id);' \
        2>/dev/null || true)"
    [[ -n "$notification_widget_id" ]] && break
    sleep 1
done
if [[ -z "$notification_widget_id" ]]; then
    printf 'Could not create the Plasma notifications widget\n' >&2
    exit 2
fi
sleep 2

"$demo" "$mode" &
demo_pid=$!
sleep 2
timeout 3 qdbus6 org.kde.kglobalaccel "/component/plasmashell" \
    org.kde.kglobalaccel.Component.invokeShortcut \
    "activate widget $notification_widget_id"
sleep 2

import -display "$DISPLAY" -window root "$full_capture"
magick "$full_capture" -crop 420x170+604+100 +repage "$output"

kill "$demo_pid" "$plasma_pid" 2>/dev/null || true
wait "$demo_pid" 2>/dev/null || true
wait "$plasma_pid" 2>/dev/null || true
