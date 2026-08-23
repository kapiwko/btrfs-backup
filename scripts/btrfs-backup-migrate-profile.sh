#!/usr/bin/env bash
set -Eeuo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/btrfs-backup-common.sh
source "$SCRIPT_DIR/lib/btrfs-backup-common.sh"

SOURCE_CONFIG="${BTRFS_BACKUP_LEGACY_CONFIG:-/etc/btrfs-backup/backup.env}"
PROFILE_CONFIG_DIR="${BTRFS_BACKUP_PROFILE_CONFIG_DIR:-/etc/btrfs-backup/profiles.d}"
SOURCE_CONFIG_DIR=""
UDEV_RULES_DIR="${BTRFS_BACKUP_UDEV_RULES_DIR:-/etc/udev/rules.d}"
PUBLIC_PROFILE_DIR="${BTRFS_BACKUP_PUBLIC_PROFILE_DIR:-/var/lib/btrfs-backup/public/profiles}"
PROFILE_ID="${BTRFS_BACKUP_PROFILE:-default}"
PROFILE_NAME="Default backup"
FORCE=0
DRY_RUN=0
REMOVE_LEGACY=0

usage() {
    cat <<'USAGE'
Usage: btrfs-backup-migrate-profile [options]

Options:
  --source PATH       Legacy configuration file (default: /etc/btrfs-backup/backup.env).
  --sources-dir PATH  Legacy source definitions directory (default: SOURCES_DIR from source).
  --profile ID        Profile id to create (default: default).
  --name TEXT         Human-readable profile name (default: Default backup).
  --profile-dir PATH  Profile directory (default: /etc/btrfs-backup/profiles.d).
  --udev-dir PATH     udev rules directory (default: /etc/udev/rules.d).
  --public-dir PATH   Public profile manifest directory.
  --force             Replace an existing profile file after saving a timestamped backup.
  --remove-legacy     Move legacy configuration, source directory, and udev rule aside.
  --dry-run           Validate inputs and print the target path without writing.
  -h, --help          Show this help.
USAGE
}

detect_profile_helper() {
    if [[ -x "$SCRIPT_DIR/../bin/btrfs-backup-profile" ]]; then
        printf '%s\n' "$SCRIPT_DIR/../bin/btrfs-backup-profile"
    elif [[ -x /usr/bin/btrfs-backup-profile ]]; then
        printf '%s\n' /usr/bin/btrfs-backup-profile
    else
        bb_die "Could not locate btrfs-backup-profile."
    fi
}

backup_existing_file() {
    local path="$1"
    local stamp backup

    [[ -e "$path" ]] || return 0
    stamp="$(date -u +%Y%m%dT%H%M%SZ)"
    backup="$path.backup-$stamp"
    cp -a -- "$path" "$backup"
    chmod 0600 "$backup"
}

move_legacy_path_aside() {
    local path="$1"
    local label="$2"
    local stamp backup

    [[ -e "$path" ]] || return 0
    stamp="$(date -u +%Y%m%dT%H%M%SZ)"
    backup="$path.migrated-$stamp"
    mv -- "$path" "$backup"
    bb_log INFO "Moved legacy $label aside: $backup"
}

assert_migration_shell_input() {
    local path="$1"
    local owner_uid mode permissions expected_uid

    if (( SYSTEM_WRITE == 1 )); then
        bb_assert_trusted_config_file "$path"
        return 0
    fi

    [[ -f "$path" ]] || bb_die "Shell input file does not exist or is not a regular file: $path"
    [[ -r "$path" ]] || bb_die "Shell input file is not readable: $path"
    owner_uid="$(stat -Lc '%u' -- "$path" 2>/dev/null || true)"
    mode="$(stat -Lc '%a' -- "$path" 2>/dev/null || true)"
    expected_uid="$EUID"
    [[ "$owner_uid" == "$expected_uid" ]] \
        || bb_die "Shell input must be owned by the invoking user (uid $expected_uid): $path"
    [[ "$mode" =~ ^[0-7]{3,4}$ ]] || bb_die "Could not determine permissions for: $path"
    permissions=$((8#$mode))
    (( (permissions & 0077) == 0 )) || bb_die "Shell input must be private (mode 0600 recommended): $path (mode $mode)"
}

while (( $# > 0 )); do
    case "$1" in
        --source)
            [[ $# -ge 2 ]] || bb_die "--source requires a path."
            SOURCE_CONFIG="$2"
            shift 2
            ;;
        --sources-dir)
            [[ $# -ge 2 ]] || bb_die "--sources-dir requires a path."
            SOURCE_CONFIG_DIR="$2"
            shift 2
            ;;
        --profile)
            [[ $# -ge 2 ]] || bb_die "--profile requires an identifier."
            PROFILE_ID="$2"
            shift 2
            ;;
        --name)
            [[ $# -ge 2 ]] || bb_die "--name requires text."
            PROFILE_NAME="$2"
            shift 2
            ;;
        --profile-dir)
            [[ $# -ge 2 ]] || bb_die "--profile-dir requires a path."
            PROFILE_CONFIG_DIR="$2"
            shift 2
            ;;
        --udev-dir)
            [[ $# -ge 2 ]] || bb_die "--udev-dir requires a path."
            UDEV_RULES_DIR="$2"
            shift 2
            ;;
        --public-dir)
            [[ $# -ge 2 ]] || bb_die "--public-dir requires a path."
            PUBLIC_PROFILE_DIR="$2"
            shift 2
            ;;
        --force)
            FORCE=1
            shift
            ;;
        --remove-legacy)
            REMOVE_LEGACY=1
            shift
            ;;
        --dry-run)
            DRY_RUN=1
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

bb_validate_safe_name PROFILE_ID "$PROFILE_ID"
[[ "$PROFILE_NAME" != *$'\n'* && "$PROFILE_NAME" != *$'\r'* && "$PROFILE_NAME" != *$'\t'* ]] \
    || bb_die "PROFILE_NAME must be a single-line value without tabs."
bb_validate_absolute_path SOURCE_CONFIG "$SOURCE_CONFIG"
bb_validate_absolute_path PROFILE_CONFIG_DIR "$PROFILE_CONFIG_DIR"
[[ -z "$SOURCE_CONFIG_DIR" ]] || bb_validate_absolute_path SOURCE_CONFIG_DIR "$SOURCE_CONFIG_DIR"
bb_validate_absolute_path UDEV_RULES_DIR "$UDEV_RULES_DIR"
bb_validate_absolute_path PUBLIC_PROFILE_DIR "$PUBLIC_PROFILE_DIR"

TARGET_CONFIG="$PROFILE_CONFIG_DIR/$PROFILE_ID.env"
PROFILE_ROOT="$(dirname -- "$PROFILE_CONFIG_DIR")"
TARGET_PROFILE_DIR="$PROFILE_ROOT/profiles/$PROFILE_ID"
TARGET_PROFILE_JSON="$TARGET_PROFILE_DIR/profile.json"
PROFILE_HELPER="$(detect_profile_helper)"
SYSTEM_WRITE=0
if [[ "$PROFILE_ROOT" == /etc/btrfs-backup ]] \
    || [[ "$UDEV_RULES_DIR" == /etc/udev/rules.d ]] \
    || [[ "$PUBLIC_PROFILE_DIR" == /var/lib/btrfs-backup/public/profiles ]]; then
    SYSTEM_WRITE=1
fi

if (( DRY_RUN == 1 )); then
    [[ -f "$SOURCE_CONFIG" ]] || bb_die "Legacy configuration does not exist: $SOURCE_CONFIG"
    printf 'Would create profile %s at %s from %s\n' "$PROFILE_ID" "$TARGET_CONFIG" "$SOURCE_CONFIG"
    printf 'Would create canonical profile JSON at %s\n' "$TARGET_PROFILE_JSON"
    exit 0
fi

if (( EUID != 0 && SYSTEM_WRITE == 1 )); then
    bb_die "Writing system profile configuration requires root."
fi
bb_require_commands chmod chown cp date install mktemp mv python3 realpath stat
assert_migration_shell_input "$SOURCE_CONFIG"

# Trusted legacy deployment input.
# shellcheck disable=SC1090
source "$SOURCE_CONFIG"

SOURCE_CONFIG_DIR="${SOURCE_CONFIG_DIR:-${SOURCES_DIR:-/etc/btrfs-backup/sources.d}}"
bb_validate_absolute_path SOURCE_CONFIG_DIR "$SOURCE_CONFIG_DIR"
source_files=("$SOURCE_CONFIG_DIR"/*.conf)
(( ${#source_files[@]} > 0 )) || bb_die "No source definitions found in $SOURCE_CONFIG_DIR"
for source_file in "${source_files[@]}"; do
    assert_migration_shell_input "$source_file"
done

BACKUP_BTRFS_UUID="${BACKUP_BTRFS_UUID:-}"
REMOTE_ROOT="${REMOTE_ROOT:-$BACKUP_MOUNTPOINT/snapshots}"
INCOMING_ROOT="${INCOMING_ROOT:-$BACKUP_MOUNTPOINT/.incoming}"
STATE_DIR="${STATE_DIR:-/var/lib/btrfs-backup}"
STATUS_ROOT="${STATUS_ROOT:-/run/btrfs-backup/profiles}"
HISTORY_ROOT="${HISTORY_ROOT:-/var/lib/btrfs-backup/history}"
RETENTION_COUNT="${RETENTION_COUNT:-30}"
LOCAL_RETENTION_COUNT="${LOCAL_RETENTION_COUNT:-$RETENTION_COUNT}"
DAILY_LIMIT="${DAILY_LIMIT:-true}"
INCREMENTAL_REQUIRED="${INCREMENTAL_REQUIRED:-true}"
KEEP_FAILED_LOCAL_SNAPSHOT="${KEEP_FAILED_LOCAL_SNAPSHOT:-false}"
AUTO_EJECT="${AUTO_EJECT:-true}"
MIN_TARGET_FREE_BYTES="${MIN_TARGET_FREE_BYTES:-5368709120}"
MIN_LOCAL_FREE_BYTES="${MIN_LOCAL_FREE_BYTES:-1073741824}"
NOTIFY_ENABLE="${NOTIFY_ENABLE:-true}"
NOTIFY_USER="${NOTIFY_USER:-}"
NOTIFY_METHOD="${NOTIFY_METHOD:-auto}"

for required in BACKUP_DEVICE BACKUP_LUKS_UUID BACKUP_MAPPER_NAME BACKUP_MOUNTPOINT; do
    bb_require_var "$required"
done

if [[ -e "$TARGET_CONFIG" && "$FORCE" != 1 ]]; then
    bb_die "Profile already exists: $TARGET_CONFIG (use --force to replace it)"
fi

install -d -m0755 "$PROFILE_CONFIG_DIR" "$TARGET_PROFILE_DIR"
if [[ -e "$TARGET_CONFIG" ]]; then
    backup_existing_file "$TARGET_CONFIG"
fi
if [[ -e "$TARGET_PROFILE_JSON" ]]; then
    backup_existing_file "$TARGET_PROFILE_JSON"
fi

sources_table="$(mktemp "$PROFILE_CONFIG_DIR/.${PROFILE_ID}.sources.XXXXXX")"
profile_json_temp="$(mktemp "$PROFILE_CONFIG_DIR/.${PROFILE_ID}.json.XXXXXX")"
chmod 0600 "$sources_table" "$profile_json_temp"
trap 'rm -f -- "$sources_table" "$profile_json_temp"' EXIT

: > "$sources_table"
for source_file in "${source_files[@]}"; do
    ENABLED=true
    SOURCE_NAME=""
    SOURCE_DISPLAY_NAME=""
    SOURCE_SUBVOLUME=""
    LOCAL_SNAPSHOT_DIR=""
    REMOTE_SUBDIR=""
    SOURCE_RETENTION_COUNT="$RETENTION_COUNT"
    SOURCE_LOCAL_RETENTION_COUNT="$LOCAL_RETENTION_COUNT"
    # Trusted legacy source definition.
    # shellcheck disable=SC1090
    source "$source_file"
    if ! bb_bool_is_true "$ENABLED"; then
        continue
    fi
    bb_validate_safe_name SOURCE_NAME "$SOURCE_NAME"
    bb_validate_absolute_path SOURCE_SUBVOLUME "$SOURCE_SUBVOLUME"
    bb_validate_absolute_path LOCAL_SNAPSHOT_DIR "$LOCAL_SNAPSHOT_DIR"
    bb_validate_relative_path REMOTE_SUBDIR "$REMOTE_SUBDIR"
    bb_validate_uint SOURCE_RETENTION_COUNT "$SOURCE_RETENTION_COUNT"
    bb_validate_uint SOURCE_LOCAL_RETENTION_COUNT "$SOURCE_LOCAL_RETENTION_COUNT"
    [[ "$SOURCE_SUBVOLUME" != *$'\t'* ]] || bb_die "SOURCE_SUBVOLUME must not contain tabs."
    [[ "$LOCAL_SNAPSHOT_DIR" != *$'\t'* ]] || bb_die "LOCAL_SNAPSHOT_DIR must not contain tabs."
    [[ "$REMOTE_SUBDIR" != *$'\t'* ]] || bb_die "REMOTE_SUBDIR must not contain tabs."
    [[ "${SOURCE_DISPLAY_NAME:-$SOURCE_NAME}" != *$'\n'* && "${SOURCE_DISPLAY_NAME:-$SOURCE_NAME}" != *$'\r'* && "${SOURCE_DISPLAY_NAME:-$SOURCE_NAME}" != *$'\t'* ]] \
        || bb_die "SOURCE_DISPLAY_NAME must be a single-line value without tabs."
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$SOURCE_NAME" \
        "${SOURCE_DISPLAY_NAME:-$SOURCE_NAME}" \
        "$SOURCE_SUBVOLUME" \
        "$LOCAL_SNAPSHOT_DIR" \
        "$REMOTE_SUBDIR" \
        "$SOURCE_RETENTION_COUNT" \
        "$SOURCE_LOCAL_RETENTION_COUNT" >> "$sources_table"
done

[[ -s "$sources_table" ]] || bb_die "No enabled source definitions found in $SOURCE_CONFIG_DIR"

PROFILE_ID="$PROFILE_ID" \
PROFILE_NAME="$PROFILE_NAME" \
PROFILE_ROOT="$PROFILE_ROOT" \
BACKUP_DEVICE="$BACKUP_DEVICE" \
BACKUP_LUKS_UUID="$BACKUP_LUKS_UUID" \
BACKUP_BTRFS_UUID="$BACKUP_BTRFS_UUID" \
BACKUP_MAPPER_NAME="$BACKUP_MAPPER_NAME" \
BACKUP_MOUNTPOINT="$BACKUP_MOUNTPOINT" \
REMOTE_ROOT="$REMOTE_ROOT" \
INCOMING_ROOT="$INCOMING_ROOT" \
STATE_DIR="$STATE_DIR" \
STATUS_ROOT="$STATUS_ROOT" \
HISTORY_ROOT="$HISTORY_ROOT" \
RETENTION_COUNT="$RETENTION_COUNT" \
LOCAL_RETENTION_COUNT="$LOCAL_RETENTION_COUNT" \
DAILY_LIMIT="$DAILY_LIMIT" \
INCREMENTAL_REQUIRED="$INCREMENTAL_REQUIRED" \
KEEP_FAILED_LOCAL_SNAPSHOT="$KEEP_FAILED_LOCAL_SNAPSHOT" \
AUTO_EJECT="$AUTO_EJECT" \
MIN_TARGET_FREE_BYTES="$MIN_TARGET_FREE_BYTES" \
MIN_LOCAL_FREE_BYTES="$MIN_LOCAL_FREE_BYTES" \
NOTIFY_ENABLE="$NOTIFY_ENABLE" \
NOTIFY_USER="$NOTIFY_USER" \
NOTIFY_METHOD="$NOTIFY_METHOD" \
python3 - "$profile_json_temp" "$sources_table" <<'PY'
import json
import os
import sys


def as_bool(name):
    return os.environ[name].lower() == "true"


def as_int(name):
    return int(os.environ[name])


profile_id = os.environ["PROFILE_ID"]
profile_root = os.environ["PROFILE_ROOT"]
if profile_root == "/etc/btrfs-backup":
    sources_dir = f"/etc/btrfs-backup/profiles/{profile_id}/sources.d"
else:
    sources_dir = f"{profile_root}/profiles/{profile_id}/sources.d"
sources = []
with open(sys.argv[2], "r", encoding="utf-8") as stream:
    for line in stream:
        source_id, display_name, subvolume, local_dir, remote_subdir, remote_retention, local_retention = line.rstrip("\n").split("\t")
        sources.append(
            {
                "id": source_id,
                "name": display_name,
                "enabled": True,
                "subvolume": subvolume,
                "localSnapshotDir": local_dir,
                "remoteSubdir": remote_subdir,
                "remoteRetention": int(remote_retention),
                "localRetention": int(local_retention),
            }
        )

profile = {
    "schemaVersion": 1,
    "profileId": profile_id,
    "name": os.environ["PROFILE_NAME"],
    "enabled": True,
    "target": {
        "device": os.environ["BACKUP_DEVICE"],
        "luksUuid": os.environ["BACKUP_LUKS_UUID"],
        "btrfsUuid": os.environ["BACKUP_BTRFS_UUID"],
        "partitionUuid": "",
        "serial": "",
        "mapperName": os.environ["BACKUP_MAPPER_NAME"],
        "mountPoint": os.environ["BACKUP_MOUNTPOINT"],
    },
    "paths": {
        "sourcesDir": sources_dir,
        "remoteRoot": os.environ["REMOTE_ROOT"],
        "incomingRoot": os.environ["INCOMING_ROOT"],
        "stateDir": os.environ["STATE_DIR"],
        "statusRoot": os.environ["STATUS_ROOT"],
        "historyRoot": os.environ["HISTORY_ROOT"],
    },
    "settings": {
        "dailyLimit": as_bool("DAILY_LIMIT"),
        "incrementalRequired": as_bool("INCREMENTAL_REQUIRED"),
        "keepFailedLocalSnapshot": as_bool("KEEP_FAILED_LOCAL_SNAPSHOT"),
        "autoEject": as_bool("AUTO_EJECT"),
        "remoteRetention": as_int("RETENTION_COUNT"),
        "localRetention": as_int("LOCAL_RETENTION_COUNT"),
        "minimumTargetFreeBytes": as_int("MIN_TARGET_FREE_BYTES"),
        "minimumLocalFreeBytes": as_int("MIN_LOCAL_FREE_BYTES"),
    },
    "notifications": {
        "enabled": as_bool("NOTIFY_ENABLE"),
        "user": os.environ["NOTIFY_USER"],
        "method": os.environ["NOTIFY_METHOD"],
    },
    "sources": sources,
}

with open(sys.argv[1], "w", encoding="utf-8") as stream:
    json.dump(profile, stream, indent=2)
    stream.write("\n")
PY

"$PROFILE_HELPER" \
    --etc-root "$PROFILE_ROOT" \
    --udev-root "$UDEV_RULES_DIR" \
    --public-root "$PUBLIC_PROFILE_DIR" \
    save --file "$profile_json_temp" >/dev/null
install -m0600 "$profile_json_temp" "$TARGET_PROFILE_JSON"
if (( EUID == 0 )); then
    chown 0:0 "$TARGET_CONFIG" "$TARGET_PROFILE_JSON"
fi

bb_log INFO "Created profile configuration: $TARGET_CONFIG"
bb_log INFO "Created canonical profile JSON: $TARGET_PROFILE_JSON"

if (( REMOVE_LEGACY == 1 )); then
    source_config_dir_real="$(realpath -m -- "$SOURCE_CONFIG_DIR")"
    target_sources_dir_real="$(realpath -m -- "$TARGET_PROFILE_DIR/sources.d")"
    legacy_udev_rule="$UDEV_RULES_DIR/99-btrfs-backup.rules"
    profile_udev_rule="$UDEV_RULES_DIR/99-btrfs-backup-$PROFILE_ID.rules"
    move_legacy_path_aside "$SOURCE_CONFIG" "configuration"
    if [[ "$source_config_dir_real" != "$target_sources_dir_real" ]]; then
        move_legacy_path_aside "$SOURCE_CONFIG_DIR" "source directory"
    fi
    if [[ "$legacy_udev_rule" != "$profile_udev_rule" ]]; then
        move_legacy_path_aside "$legacy_udev_rule" "udev rule"
    fi
fi
