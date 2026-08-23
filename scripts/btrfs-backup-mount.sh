#!/usr/bin/env bash
set -Eeuo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/btrfs-backup-common.sh
source "$SCRIPT_DIR/lib/btrfs-backup-common.sh"

REQUESTED_PROFILE_ID="${BTRFS_BACKUP_PROFILE:-default}"
PROFILE_WAS_REQUESTED=0
[[ -n "${BTRFS_BACKUP_PROFILE:-}" ]] && PROFILE_WAS_REQUESTED=1
PROFILE_CONFIG_DIR="${BTRFS_BACKUP_PROFILE_CONFIG_DIR:-/etc/btrfs-backup/profiles.d}"
CONFIG_FILE="${BTRFS_BACKUP_CONFIG:-}"
PROFILE_JSON_FILE="${BTRFS_BACKUP_PROFILE_JSON:-}"

usage() {
    cat <<'USAGE'
Usage: btrfs-backup-mount [options]

Options:
  --profile ID      Use /etc/btrfs-backup/profiles/ID/profile.json.
  --config PATH      Use a non-default generated env configuration file.
  -h, --help         Show this help.
USAGE
}

while (( $# > 0 )); do
    case "$1" in
        --config)
            [[ $# -ge 2 ]] || bb_die "--config requires a path."
            CONFIG_FILE="$2"
            shift 2
            ;;
        --profile)
            [[ $# -ge 2 ]] || bb_die "--profile requires an identifier."
            REQUESTED_PROFILE_ID="$2"
            PROFILE_WAS_REQUESTED=1
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            bb_die "Unknown option: $1"
            ;;
    esac
done

bb_require_root
bb_require_commands cryptsetup findmnt install mountpoint readlink systemctl systemd-escape
if [[ -n "$CONFIG_FILE" ]]; then
    CONFIG_FILE="$(bb_resolve_profile_config "$REQUESTED_PROFILE_ID" "$CONFIG_FILE" "$PROFILE_CONFIG_DIR")"
    bb_load_config "$CONFIG_FILE"
else
    PROFILE_JSON_FILE="$(bb_resolve_profile_json "$REQUESTED_PROFILE_ID" "$PROFILE_JSON_FILE" "$PROFILE_CONFIG_DIR")"
    bb_load_profile_json_config "$PROFILE_JSON_FILE"
fi

PROFILE_ID="${PROFILE_ID:-$REQUESTED_PROFILE_ID}"
if (( PROFILE_WAS_REQUESTED == 1 )) && [[ "$PROFILE_ID" != "$REQUESTED_PROFILE_ID" ]]; then
    bb_die "Requested profile $REQUESTED_PROFILE_ID but loaded profile declares PROFILE_ID=$PROFILE_ID"
fi
BACKUP_BTRFS_UUID="${BACKUP_BTRFS_UUID:-}"

for required in BACKUP_MAPPER_NAME BACKUP_MOUNTPOINT BACKUP_DEVICE BACKUP_LUKS_UUID BACKUP_MOUNT_UNIT; do
    bb_require_var "$required"
done
bb_validate_safe_name BACKUP_MAPPER_NAME "$BACKUP_MAPPER_NAME"
bb_validate_absolute_path BACKUP_MOUNTPOINT "$BACKUP_MOUNTPOINT"
bb_validate_absolute_path BACKUP_DEVICE "$BACKUP_DEVICE"

expected_mount_unit="$(systemd-escape -p --suffix=mount "$BACKUP_MOUNTPOINT")"
if [[ "$BACKUP_MOUNT_UNIT" != "$expected_mount_unit" ]]; then
    bb_die "BACKUP_MOUNT_UNIT=$BACKUP_MOUNT_UNIT does not match $BACKUP_MOUNTPOINT (expected $expected_mount_unit)."
fi

actual_luks_uuid="$(cryptsetup luksUUID "$BACKUP_DEVICE" 2>/dev/null || true)"
if [[ -z "$actual_luks_uuid" || "${actual_luks_uuid,,}" != "${BACKUP_LUKS_UUID,,}" ]]; then
    bb_die "LUKS UUID mismatch for $BACKUP_DEVICE"
fi

install -d -m0755 "$BACKUP_MOUNTPOINT"
if ! mountpoint -q "$BACKUP_MOUNTPOINT"; then
    bb_log INFO "Mounting encrypted backup target."
    systemctl start "$BACKUP_MOUNT_UNIT"
fi

if ! mountpoint -q "$BACKUP_MOUNTPOINT"; then
    bb_die "Backup target is not mounted at $BACKUP_MOUNTPOINT"
fi

if [[ "$(bb_mount_fstype "$BACKUP_MOUNTPOINT")" != btrfs ]]; then
    bb_die "Backup target is not a Btrfs filesystem: $BACKUP_MOUNTPOINT"
fi

if ! bb_mount_uses_mapper "$BACKUP_MOUNTPOINT" "$BACKUP_MAPPER_NAME"; then
    bb_die "The filesystem mounted at $BACKUP_MOUNTPOINT is not /dev/mapper/$BACKUP_MAPPER_NAME"
fi

configured_real="$(bb_canonical_device "$BACKUP_DEVICE")"
actual_device="$(bb_mapper_underlying_device "$BACKUP_MAPPER_NAME")"
actual_real="$(bb_canonical_device "$actual_device")"
if [[ -z "$configured_real" || -z "$actual_real" || "$configured_real" != "$actual_real" ]]; then
    bb_die "LUKS mapper $BACKUP_MAPPER_NAME does not use configured device $BACKUP_DEVICE (actual: ${actual_device:-unknown})."
fi

if [[ -n "$BACKUP_BTRFS_UUID" ]]; then
    actual_btrfs_uuid="$(findmnt -n -o UUID -M "$BACKUP_MOUNTPOINT" 2>/dev/null || true)"
    if [[ -z "$actual_btrfs_uuid" || "${actual_btrfs_uuid,,}" != "${BACKUP_BTRFS_UUID,,}" ]]; then
        bb_die "Btrfs UUID mismatch at $BACKUP_MOUNTPOINT"
    fi
fi

bb_log INFO "Backup target is mounted at $BACKUP_MOUNTPOINT."
