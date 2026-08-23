#!/usr/bin/env bash
set -Eeuo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/btrfs-backup-common.sh
source "$SCRIPT_DIR/lib/btrfs-backup-common.sh"

SOURCE_CONFIG="${BTRFS_BACKUP_LEGACY_CONFIG:-/etc/btrfs-backup/backup.env}"
PROFILE_CONFIG_DIR="${BTRFS_BACKUP_PROFILE_CONFIG_DIR:-/etc/btrfs-backup/profiles.d}"
PROFILE_ID="${BTRFS_BACKUP_PROFILE:-default}"
PROFILE_NAME="Default backup"
FORCE=0
DRY_RUN=0

usage() {
    cat <<'USAGE'
Usage: btrfs-backup-migrate-profile [options]

Options:
  --source PATH       Legacy configuration file (default: /etc/btrfs-backup/backup.env).
  --profile ID        Profile id to create (default: default).
  --name TEXT         Human-readable profile name (default: Default backup).
  --profile-dir PATH  Profile directory (default: /etc/btrfs-backup/profiles.d).
  --force             Replace an existing profile file after saving a timestamped backup.
  --dry-run           Validate inputs and print the target path without writing.
  -h, --help          Show this help.
USAGE
}

shell_quote() {
    printf '%q' "$1"
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

while (( $# > 0 )); do
    case "$1" in
        --source)
            [[ $# -ge 2 ]] || bb_die "--source requires a path."
            SOURCE_CONFIG="$2"
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
        --force)
            FORCE=1
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
bb_validate_absolute_path SOURCE_CONFIG "$SOURCE_CONFIG"
bb_validate_absolute_path PROFILE_CONFIG_DIR "$PROFILE_CONFIG_DIR"

TARGET_CONFIG="$PROFILE_CONFIG_DIR/$PROFILE_ID.env"

if (( DRY_RUN == 1 )); then
    [[ -f "$SOURCE_CONFIG" ]] || bb_die "Legacy configuration does not exist: $SOURCE_CONFIG"
    printf 'Would create profile %s at %s from %s\n' "$PROFILE_ID" "$TARGET_CONFIG" "$SOURCE_CONFIG"
    exit 0
fi

bb_require_root
bb_require_commands cat chmod chown cp date install mktemp mv stat
bb_assert_trusted_config_file "$SOURCE_CONFIG"

if [[ -e "$TARGET_CONFIG" && "$FORCE" != 1 ]]; then
    bb_die "Profile already exists: $TARGET_CONFIG (use --force to replace it)"
fi

install -d -m0755 "$PROFILE_CONFIG_DIR"
if [[ -e "$TARGET_CONFIG" ]]; then
    backup_existing_file "$TARGET_CONFIG"
fi

temp_file="$(mktemp "$PROFILE_CONFIG_DIR/.${PROFILE_ID}.XXXXXX")"
chmod 0600 "$temp_file"
{
    printf '# Migrated from %s by btrfs-backup-migrate-profile.\n' "$SOURCE_CONFIG"
    printf '# The legacy file is left in place for compatibility.\n'
    cat -- "$SOURCE_CONFIG"
    printf '\n# Profile metadata added during migration.\n'
    printf 'PROFILE_ID=%s\n' "$(shell_quote "$PROFILE_ID")"
    printf 'PROFILE_NAME=%s\n' "$(shell_quote "$PROFILE_NAME")"
} > "$temp_file"
mv -f -- "$temp_file" "$TARGET_CONFIG"
chmod 0600 "$TARGET_CONFIG"
chown 0:0 "$TARGET_CONFIG"

bb_log INFO "Created profile configuration: $TARGET_CONFIG"
