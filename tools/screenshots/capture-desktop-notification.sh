#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

mode="${BTRFS_BACKUP_SCREENSHOT_MODE:?}"
output="${BTRFS_BACKUP_SCREENSHOT_OUTPUT:?}"
demo="${BTRFS_BACKUP_SCREENSHOT_DEMO:?}"

plasmawindowed org.kde.plasma.notifications &
plasma_pid=$!
sleep 2

"$demo" "$mode" &
demo_pid=$!
sleep 2

spectacle --new-instance --activewindow --background --nonotify --no-shadow \
    --output "$output"

kill "$demo_pid" "$plasma_pid" 2>/dev/null || true
wait "$demo_pid" 2>/dev/null || true
wait "$plasma_pid" 2>/dev/null || true
