#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${BTRFS_BACKUP_SCREENSHOT_IMAGE:-btrfs-backup-screenshots:local}"

docker build \
    --tag "$image" \
    --file "$root_dir/tools/screenshots/Dockerfile" \
    "$root_dir/tools/screenshots"

docker run --rm \
    --cap-add SYS_NICE \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env BTRFS_BACKUP_SCREENSHOT_BUILD_DIR=/workspace/build/readme-screenshots-container \
    --env BTRFS_BACKUP_SCREENSHOT_DEBUG \
    --env BTRFS_BACKUP_SCREENSHOT_KCM_PAGE \
    --env BTRFS_BACKUP_SCREENSHOT_ONLY \
    --volume /etc/passwd:/etc/passwd:ro \
    --volume /etc/group:/etc/group:ro \
    --volume "$root_dir:/workspace" \
    --workdir /workspace \
    "$image" \
    ./tools/render-readme-screenshot.sh "$@"
