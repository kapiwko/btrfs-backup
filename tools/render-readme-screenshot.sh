#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QML_EXECUTABLE="${QML_EXECUTABLE:-}"
BUILD_DIR="${BTRFS_BACKUP_SCREENSHOT_BUILD_DIR:-$ROOT_DIR/build/readme-screenshots}"

if [[ -z "$QML_EXECUTABLE" ]]; then
    QML_EXECUTABLE="$(command -v qml6 || command -v qml || true)"
fi
if [[ -z "$QML_EXECUTABLE" ]]; then
    printf 'qml6 or qml is required to render the screenshot\n' >&2
    exit 1
fi

OUTPUT_DIR="${1:-$ROOT_DIR/docs/images}"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath -m "$OUTPUT_DIR")"

cmake \
    -S "$ROOT_DIR" \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_KDE_INTEGRATION=ON \
    -DBUILD_README_SCREENSHOTS=ON \
    -DBUILD_TESTING=OFF
cmake \
    --build "$BUILD_DIR" \
    --target \
        btrfsbackup_plasma_backendplugin \
        btrfs-backup-kde-restore \
        btrfs-backup-kde-screenshot-demo \
    --parallel

render_window() {
    local scene="$1"
    local output="$2"
    local delay="${3:-1}"
    local runtime_dir="/tmp/btrfs-backup-readme-$UID/window-${output%.png}"

    cmake -E remove_directory "$runtime_dir"
    cmake -E make_directory "$runtime_dir"
    chmod 700 "$runtime_dir"
    rm -f "$OUTPUT_DIR/$output"
    XDG_RUNTIME_DIR="$runtime_dir" \
    BTRFS_BACKUP_SCREENSHOT_SCENE="$scene" \
    BTRFS_BACKUP_SCREENSHOT_OUTPUT="$OUTPUT_DIR/$output" \
    BTRFS_BACKUP_SCREENSHOT_QML="$QML_EXECUTABLE" \
    BTRFS_BACKUP_SCREENSHOT_BUILD_DIR="$BUILD_DIR" \
    BTRFS_BACKUP_SCREENSHOT_DELAY="$delay" \
        dbus-run-session -- \
        kwin_wayland \
            --virtual \
            --socket wayland-bb-window \
            --width 1024 \
            --height 768 \
            --no-lockscreen \
            --no-global-shortcuts \
            --exit-with-session \
            "$ROOT_DIR/tools/screenshots/capture-active-window.sh"

    if [[ ! -f "$OUTPUT_DIR/$output" ]]; then
        printf 'Expected screenshot was not created: %s\n' "$OUTPUT_DIR/$output" >&2
        exit 2
    fi
    printf 'Rendered %s\n' "$OUTPUT_DIR/$output"
}

render_desktop_notification() {
    local mode="$1"
    local output="$2"
    local data_dir="$BUILD_DIR/screenshot-data"
    local runtime_dir="/tmp/btrfs-backup-readme-$UID/notification-$mode"
    local demo="$BUILD_DIR/integrations/kde/btrfs-backup-kde-screenshot-demo"

    cmake -E make_directory "$data_dir/knotifications6"
    cmake -E copy \
        "$ROOT_DIR/integrations/kde/monitor/btrfs-backup-kde-monitor.notifyrc" \
        "$data_dir/knotifications6/btrfs-backup-kde-monitor.notifyrc"
    cmake -E remove_directory "$runtime_dir"
    cmake -E make_directory "$runtime_dir"
    chmod 700 "$runtime_dir"
    rm -f "$OUTPUT_DIR/$output"

    XDG_RUNTIME_DIR="$runtime_dir" \
    XDG_DATA_HOME="$data_dir" \
    BTRFS_BACKUP_SCREENSHOT_MODE="$mode" \
    BTRFS_BACKUP_SCREENSHOT_OUTPUT="$OUTPUT_DIR/$output" \
    BTRFS_BACKUP_SCREENSHOT_DEMO="$demo" \
        dbus-run-session -- \
        kwin_wayland \
            --virtual \
            --socket wayland-bb-notification \
            --width 640 \
            --height 700 \
            --no-lockscreen \
            --no-global-shortcuts \
            --exit-with-session \
            "$ROOT_DIR/tools/screenshots/capture-desktop-notification.sh"

    if [[ ! -f "$OUTPUT_DIR/$output" ]]; then
        printf 'Expected screenshot was not created: %s\n' "$OUTPUT_DIR/$output" >&2
        exit 2
    fi
    printf 'Rendered %s\n' "$OUTPUT_DIR/$output"
}

render_window "$ROOT_DIR/tools/screenshots/plasma-widget-collapsed.qml" "plasma-widget-collapsed.png"
render_window "$ROOT_DIR/tools/screenshots/plasma-widget-connected.qml" "plasma-widget-connected.png"
render_window "$ROOT_DIR/tools/screenshots/plasma-widget-disconnected.qml" "plasma-widget-disconnected.png"
render_window "$ROOT_DIR/tools/screenshots/plasma-widget-transferring.qml" "plasma-widget-transferring.png" 4
render_desktop_notification transfer "kui-job-transferring.png"
render_desktop_notification completion "notification-completed.png"
render_window "$ROOT_DIR/tools/screenshots/restore-dialog.qml" "restore-dialog.png"
render_window "$ROOT_DIR/tools/screenshots/system-settings-connected.qml" "system-settings-connected.png"
render_window "$ROOT_DIR/tools/screenshots/system-settings-disconnected.qml" "system-settings-disconnected.png"
render_window "$ROOT_DIR/tools/screenshots/system-settings-transferring.qml" "system-settings-transferring.png"
