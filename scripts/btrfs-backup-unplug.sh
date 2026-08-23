#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
printf '%s\n' 'btrfs-backup-unplug is deprecated; use btrfs-backup-eject.' >&2
exec "$SCRIPT_DIR/btrfs-backup-eject.sh" "$@"
