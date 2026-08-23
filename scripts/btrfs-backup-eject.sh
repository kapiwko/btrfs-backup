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
FROM_SERVICE=0
FROM_RUNNER=0
FORCE=0

usage() {
    cat <<'USAGE'
Usage: btrfs-backup-eject [options]

Options:
  --profile ID      Use /etc/btrfs-backup/profiles/ID/profile.json.
  --config PATH      Use a non-default generated env configuration file.
  --force            Continue despite target identity mismatches.
  --from-service     Internal mode used by systemd ExecStopPost.
  --from-runner      Internal mode used after a manual backup run.
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
        --force)
            FORCE=1
            shift
            ;;
        --from-service)
            FROM_SERVICE=1
            shift
            ;;
        --from-runner)
            FROM_RUNNER=1
            shift
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
bb_require_commands blockdev cryptsetup findmnt flock install mountpoint readlink stat sync systemctl systemd-escape udevadm umount
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
AUTO_EJECT="${AUTO_EJECT:-true}"
NOTIFY_ENABLE="${NOTIFY_ENABLE:-true}"
NOTIFY_METHOD="${NOTIFY_METHOD:-auto}"
NOTIFY_USER="${NOTIFY_USER:-}"
LOCK_FILE="${LOCK_FILE:-/run/btrfs-backup/backup.lock}"

for required in BACKUP_MAPPER_NAME BACKUP_MOUNTPOINT BACKUP_DEVICE BACKUP_LUKS_UUID LOCK_FILE; do
    bb_require_var "$required"
done
bb_validate_safe_name BACKUP_MAPPER_NAME "$BACKUP_MAPPER_NAME"
bb_validate_absolute_path BACKUP_MOUNTPOINT "$BACKUP_MOUNTPOINT"
bb_validate_absolute_path BACKUP_DEVICE "$BACKUP_DEVICE"
bb_validate_absolute_path LOCK_FILE "$LOCK_FILE"
bb_validate_bool AUTO_EJECT "$AUTO_EJECT"

if (( FROM_SERVICE == 1 || FROM_RUNNER == 1 )) && ! bb_bool_is_true "$AUTO_EJECT"; then
    bb_log INFO "Automatic eject is disabled by configuration."
    exit 0
fi

if ! bb_acquire_lock "$LOCK_FILE"; then
    bb_die "A backup operation is still running; refusing to eject the target."
fi
trap bb_release_lock EXIT

validate_mapper_identity() {
    local actual_device configured_real actual_real actual_luks_uuid
    actual_device="$(bb_mapper_underlying_device "$BACKUP_MAPPER_NAME")"
    configured_real="$(bb_canonical_device "$BACKUP_DEVICE")"
    actual_real="$(bb_canonical_device "$actual_device")"

    if [[ -z "$configured_real" || -z "$actual_real" || "$configured_real" != "$actual_real" ]]; then
        return 1
    fi

    actual_luks_uuid="$(cryptsetup luksUUID "$BACKUP_DEVICE" 2>/dev/null || true)"
    [[ -n "$actual_luks_uuid" && "${actual_luks_uuid,,}" == "${BACKUP_LUKS_UUID,,}" ]]
}

mapper_has_mounts() {
    local expected actual source target
    expected="$(bb_canonical_device "/dev/mapper/$BACKUP_MAPPER_NAME")"
    [[ -n "$expected" ]] || return 1

    while read -r source target; do
        source="$(bb_strip_subvolume_suffix "$source")"
        actual="$(bb_canonical_device "$source")"
        if [[ -n "$actual" && "$actual" == "$expected" ]]; then
            bb_warn "Mapper is still mounted at: $target"
            return 0
        fi
    done < <(findmnt -rn -o SOURCE,TARGET)
    return 1
}

cryptsetup_unit_name() {
    systemd-escape --template=systemd-cryptsetup@.service "$BACKUP_MAPPER_NAME"
}

bb_log INFO "Synchronizing filesystems before eject."
sync

if mountpoint -q "$BACKUP_MOUNTPOINT"; then
    if (( FORCE == 0 )) && ! bb_mount_uses_mapper "$BACKUP_MOUNTPOINT" "$BACKUP_MAPPER_NAME"; then
        bb_die "Refusing to unmount $BACKUP_MOUNTPOINT because it is not backed by /dev/mapper/$BACKUP_MAPPER_NAME"
    fi

    bb_log INFO "Unmounting $BACKUP_MOUNTPOINT"
    umount -- "$BACKUP_MOUNTPOINT"
    if mountpoint -q "$BACKUP_MOUNTPOINT"; then
        bb_die "Backup target is still mounted at $BACKUP_MOUNTPOINT"
    fi
fi

crypt_unit="$(cryptsetup_unit_name)"
bb_log INFO "Stopping LUKS systemd unit $crypt_unit"
systemctl stop "$crypt_unit" 2>/dev/null || true

if [[ -e "/dev/mapper/$BACKUP_MAPPER_NAME" ]]; then
    if (( FORCE == 0 )) && ! validate_mapper_identity; then
        bb_die "Refusing to close mapper $BACKUP_MAPPER_NAME because its underlying device does not match configuration."
    fi
    if mapper_has_mounts; then
        bb_die "Refusing to close mapper $BACKUP_MAPPER_NAME while it still has mounted filesystems."
    fi

    bb_log INFO "Closing LUKS mapper $BACKUP_MAPPER_NAME"
    cryptsetup close "$BACKUP_MAPPER_NAME"
fi

if [[ -b "$BACKUP_DEVICE" ]]; then
    blockdev --flushbufs "$BACKUP_DEVICE" 2>/dev/null || true
fi
udevadm settle --timeout=10 2>/dev/null || true

if (( FROM_SERVICE == 1 )) && [[ -n "${SERVICE_RESULT:-}" && "${SERVICE_RESULT:-}" != success ]]; then
    bb_notify "Backup did not finish successfully, but the target was unmounted and the LUKS mapper was closed. It can be disconnected."
else
    bb_notify "Backup target was safely unmounted and the LUKS mapper was closed. It can be disconnected."
fi
