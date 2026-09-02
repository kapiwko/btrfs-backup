#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C.UTF-8

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="$ROOT/dist"

log_stage() {
    printf '\n[local install %(%H:%M:%S)T] %s\n' -1 "$*" >&2
}

report_failure() {
    local status="$?" command="$BASH_COMMAND" line="${BASH_LINENO[0]}"
    printf '\n[local install %(%H:%M:%S)T] FAILED at line %s: %s\n' \
        -1 "$line" "$command" >&2
    exit "$status"
}

trap report_failure ERR

usage() {
    cat <<'USAGE'
Usage: tools/install-local-release.sh [build options]

Builds both Arch Linux packages, installs them, restarts btrfs-backupd, refreshes
the KDE service cache, and restarts the KDE monitor and Plasma Shell.

Build options such as --static-tests, --clean-build, and --build-dir are passed
to build-release.sh. The target and dist directory are fixed to the two
packages in dist/. Full tests must be run separately because they require the
release builder itself to run as root.
USAGE
}

for argument in "$@"; do
    case "$argument" in
        -h|--help)
            usage
            exit 0
            ;;
        --target|--dist-dir)
            printf '%s is fixed by this script and cannot be overridden.\n' "$argument" >&2
            exit 2
            ;;
        --full-tests)
            printf '%s\n' 'Run full tests separately with sudo before using this desktop-user deployment script.' >&2
            exit 2
            ;;
    esac
done

if (( EUID == 0 )); then
    printf '%s\n' 'Run this script as your desktop user; it invokes sudo only for system package and service operations.' >&2
    exit 1
fi

for command_name in pacman sudo systemctl kbuildsycoca6; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf 'Missing required command: %s\n' "$command_name" >&2
        exit 1
    fi
done

log_stage 'Building Arch Linux release packages'
"$ROOT/tools/build-release.sh" --target arch "$@"

base_packages=("$DIST_DIR"/btrfs-backup-[0-9]*.pkg.tar.zst)
kde_packages=("$DIST_DIR"/btrfs-backup-kde-[0-9]*.pkg.tar.zst)
if (( ${#base_packages[@]} != 1 || ${#kde_packages[@]} != 1 )); then
    printf 'Expected exactly one base package and one KDE package in %s.\n' "$DIST_DIR" >&2
    printf 'Base packages found: %s; KDE packages found: %s.\n' \
        "${#base_packages[@]}" "${#kde_packages[@]}" >&2
    exit 1
fi

log_stage 'Installing the base and KDE packages'
sudo pacman -U --noconfirm -- "${base_packages[0]}" "${kde_packages[0]}"

log_stage 'Reloading and restarting the system manager'
sudo systemctl daemon-reload
sudo systemctl restart btrfs-backupd.service

log_stage 'Refreshing the KDE user session'
systemctl --user daemon-reload
kbuildsycoca6
systemctl --user restart btrfs-backup-kde-monitor.service
systemctl --user restart plasma-plasmashell.service

log_stage 'Local release installed successfully'
printf 'Installed packages:\n  %s\n  %s\n' "${base_packages[0]}" "${kde_packages[0]}"
