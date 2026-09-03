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
ONLY="${BTRFS_BACKUP_SCREENSHOT_ONLY:-all}"
KCM_PAGE="${BTRFS_BACKUP_SCREENSHOT_KCM_PAGE:-all}"
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
        btrfs-backup-kde-manager-demo \
        kio_btrfsbackup \
        kcm_btrfsbackup \
        btrfs-backup-kde-restore \
        btrfs-backup-kde-screenshot-demo \
    --parallel

export DISPLAY=:99
Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true; wait "$xvfb_pid" 2>/dev/null || true' EXIT
sleep 1

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
    BTRFS_BACKUP_SCREENSHOT_SOURCE_DIR="$ROOT_DIR" \
    BTRFS_BACKUP_SCREENSHOT_DELAY="$delay" \
        dbus-run-session -- \
        kwin_wayland \
            --x11-display "$DISPLAY" \
            --fullscreen 1 \
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
    BTRFS_BACKUP_SCREENSHOT_DEMO="$BUILD_DIR/integrations/kde/btrfs-backup-kde-screenshot-demo" \
    BTRFS_BACKUP_SCREENSHOT_BUILD_DIR="$BUILD_DIR" \
        dbus-run-session -- \
        kwin_wayland \
            --x11-display "$DISPLAY" \
            --fullscreen 1 \
            --socket wayland-bb-notification \
            --width 1024 \
            --height 768 \
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

render_system_settings() {
    local mode="$1"
    local output="$2"
    local page="${3:-}"
    local runtime_dir="/tmp/btrfs-backup-readme-$UID/system-settings-$mode"

    cmake -E remove_directory "$runtime_dir"
    cmake -E make_directory "$runtime_dir"
    chmod 700 "$runtime_dir"
    rm -f "$OUTPUT_DIR/$output"
    XDG_RUNTIME_DIR="$runtime_dir" \
    BTRFS_BACKUP_SCREENSHOT_MODE="$mode" \
    BTRFS_BACKUP_SCREENSHOT_PAGE="$page" \
    BTRFS_BACKUP_SCREENSHOT_OUTPUT="$OUTPUT_DIR/$output" \
    BTRFS_BACKUP_SCREENSHOT_BUILD_DIR="$BUILD_DIR" \
    BTRFS_BACKUP_SCREENSHOT_SOURCE_DIR="$ROOT_DIR" \
        dbus-run-session -- \
        kwin_wayland \
            --x11-display "$DISPLAY" \
            --fullscreen 1 \
            --socket wayland-bb-kcm \
            --width 1024 \
            --height 768 \
            --no-lockscreen \
            --no-global-shortcuts \
            --exit-with-session \
            "$ROOT_DIR/tools/screenshots/capture-system-settings.sh"

    if [[ ! -f "$OUTPUT_DIR/$output" ]]; then
        printf 'Expected screenshot was not created: %s\n' "$OUTPUT_DIR/$output" >&2
        exit 2
    fi
    printf 'Rendered %s\n' "$OUTPUT_DIR/$output"
}

render_plasma_widget() {
    local mode="$1"
    local output="$2"
    local runtime_dir="/tmp/btrfs-backup-readme-$UID/plasma-widget-$mode"

    cmake -E remove_directory "$runtime_dir"
    cmake -E make_directory "$runtime_dir"
    chmod 700 "$runtime_dir"
    rm -f "$OUTPUT_DIR/$output"
    XDG_RUNTIME_DIR="$runtime_dir" \
    BTRFS_BACKUP_SCREENSHOT_MODE="$mode" \
    BTRFS_BACKUP_SCREENSHOT_OUTPUT="$OUTPUT_DIR/$output" \
    BTRFS_BACKUP_SCREENSHOT_BUILD_DIR="$BUILD_DIR" \
    BTRFS_BACKUP_SCREENSHOT_SOURCE_DIR="$ROOT_DIR" \
        dbus-run-session -- \
        kwin_wayland \
            --x11-display "$DISPLAY" \
            --fullscreen 1 \
            --socket wayland-bb-plasmoid \
            --width 1024 \
            --height 768 \
            --no-lockscreen \
            --no-global-shortcuts \
            --exit-with-session \
            "$ROOT_DIR/tools/screenshots/capture-plasma-widget.sh"

    if [[ ! -f "$OUTPUT_DIR/$output" ]]; then
        printf 'Expected screenshot was not created: %s\n' "$OUTPUT_DIR/$output" >&2
        exit 2
    fi
    printf 'Rendered %s\n' "$OUTPUT_DIR/$output"
}

render_dolphin() {
    local output="$1"
    local runtime_dir="/tmp/btrfs-backup-readme-$UID/dolphin"

    cmake -E remove_directory "$runtime_dir"
    cmake -E make_directory "$runtime_dir"
    chmod 700 "$runtime_dir"
    rm -f "$OUTPUT_DIR/$output"
    XDG_RUNTIME_DIR="$runtime_dir" \
    BTRFS_BACKUP_SCREENSHOT_OUTPUT="$OUTPUT_DIR/$output" \
    BTRFS_BACKUP_SCREENSHOT_BUILD_DIR="$BUILD_DIR" \
        dbus-run-session -- \
        kwin_wayland \
            --x11-display "$DISPLAY" \
            --fullscreen 1 \
            --socket wayland-bb-dolphin \
            --width 1024 \
            --height 768 \
            --no-lockscreen \
            --no-global-shortcuts \
            --exit-with-session \
            "$ROOT_DIR/tools/screenshots/capture-dolphin.sh"

    if [[ ! -f "$OUTPUT_DIR/$output" ]]; then
        printf 'Expected screenshot was not created: %s\n' "$OUTPUT_DIR/$output" >&2
        exit 2
    fi
    printf 'Rendered %s\n' "$OUTPUT_DIR/$output"
}

if [[ "$ONLY" == "all" || "$ONLY" == "plasma" ]]; then
    render_plasma_widget connected "plasma-widget-connected.png"
    render_plasma_widget disconnected "plasma-widget-disconnected.png"
    render_plasma_widget transferring "plasma-widget-transferring.png"
fi
if [[ "$ONLY" == "all" || "$ONLY" == "notifications" ]]; then
    render_desktop_notification transfer "kui-job-transferring.png"
    render_desktop_notification completion "notification-completed.png"
fi
if [[ "$ONLY" == "all" || "$ONLY" == "restore" ]]; then
    render_window "$ROOT_DIR/tools/screenshots/restore-dialog.qml" "restore-dialog.png"
fi
if [[ "$ONLY" == "all" || "$ONLY" == "dolphin" ]]; then
    render_dolphin "dolphin-browse.png"
fi
if [[ "$ONLY" == "all" || "$ONLY" == "kcm" ]]; then
    if [[ "$KCM_PAGE" == "all" || "$KCM_PAGE" == "overview" ]]; then
        render_system_settings connected "system-settings-connected.png"
        render_system_settings disconnected "system-settings-disconnected.png"
        render_system_settings transferring "system-settings-transferring.png"
    fi
    if [[ "$KCM_PAGE" == "all" || "$KCM_PAGE" == "profile-details" ]]; then
        render_system_settings connected "system-settings-profile-details.png" profile-details
    fi
    if [[ "$KCM_PAGE" == "all" || "$KCM_PAGE" == "profile-settings" ]]; then
        render_system_settings connected "system-settings-profile-settings.png" profile-settings
    fi
    if [[ "$KCM_PAGE" == "all" || "$KCM_PAGE" == "history" ]]; then
        render_system_settings connected "system-settings-history.png" history
    fi
    if [[ "$KCM_PAGE" == "all" || "$KCM_PAGE" == "new-profile" ]]; then
        render_system_settings connected "system-settings-new-profile.png" new-profile
    fi
    if [[ "$KCM_PAGE" == "all" || "$KCM_PAGE" == "new-profile-adopt" ]]; then
        render_system_settings connected "system-settings-new-profile-adopt.png" new-profile-adopt
    fi
    if [[ "$KCM_PAGE" == "all" || "$KCM_PAGE" == "new-profile-adopt-partition" ]]; then
        render_system_settings connected "system-settings-new-profile-adopt-partition.png" new-profile-adopt-partition
    fi
    if [[ "$KCM_PAGE" == "all" || "$KCM_PAGE" == "new-profile-prepare" ]]; then
        render_system_settings connected "system-settings-new-profile-prepare.png" new-profile-prepare
    fi
    if [[ "$KCM_PAGE" == "all" || "$KCM_PAGE" == "new-profile-prepare-partition" ]]; then
        render_system_settings connected "system-settings-new-profile-prepare-partition.png" new-profile-prepare-partition
    fi
fi
