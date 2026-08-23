#!/usr/bin/env bash
# Shared helpers for btrfs-backup.

if [[ -n "${BTRFS_BACKUP_COMMON_LOADED:-}" ]]; then
    return 0
fi
readonly BTRFS_BACKUP_COMMON_LOADED=1
readonly BTRFS_BACKUP_VERSION='1.1.0'

export LC_ALL=C

bb_timestamp() {
    date --iso-8601=seconds
}

bb_log() {
    local level="$1"
    shift
    printf '%s [%s] %s\n' "$(bb_timestamp)" "$level" "$*"
}

bb_die() {
    bb_log ERROR "$*" >&2
    exit 1
}

bb_warn() {
    bb_log WARNING "$*" >&2
}

bb_bool_is_true() {
    case "${1,,}" in
        1|true|yes|on) return 0 ;;
        *) return 1 ;;
    esac
}

bb_bool_is_false() {
    case "${1,,}" in
        0|false|no|off) return 0 ;;
        *) return 1 ;;
    esac
}

bb_validate_bool() {
    local name="$1"
    local value="$2"
    if ! bb_bool_is_true "$value" && ! bb_bool_is_false "$value"; then
        bb_die "$name must be true or false; got: $value"
    fi
}

bb_require_root() {
    if (( EUID != 0 )); then
        bb_die "This command must be run as root."
    fi
}

bb_require_commands() {
    local command_name
    local missing=()
    for command_name in "$@"; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            missing+=("$command_name")
        fi
    done
    if (( ${#missing[@]} > 0 )); then
        bb_die "Missing required commands: ${missing[*]}"
    fi
}

bb_assert_trusted_config_file() {
    local config_file="$1"
    local owner_uid mode permissions

    if [[ ! -f "$config_file" ]]; then
        bb_die "Configuration file does not exist or is not a regular file: $config_file"
    fi
    if [[ ! -r "$config_file" ]]; then
        bb_die "Configuration file is not readable: $config_file"
    fi

    owner_uid="$(stat -Lc '%u' -- "$config_file" 2>/dev/null || true)"
    mode="$(stat -Lc '%a' -- "$config_file" 2>/dev/null || true)"
    if [[ "$owner_uid" != 0 ]]; then
        bb_die "Trusted shell configuration must be owned by root: $config_file"
    fi
    if [[ ! "$mode" =~ ^[0-7]{3,4}$ ]]; then
        bb_die "Could not determine configuration permissions: $config_file"
    fi

    permissions=$((8#$mode))
    if (( permissions & 0077 )); then
        bb_die "Trusted shell configuration must not be accessible by group or others: $config_file (mode $mode)"
    fi
}

bb_load_config() {
    local config_file="$1"
    bb_assert_trusted_config_file "$config_file"

    # Configuration files are deliberately trusted root-owned Bash syntax.
    # shellcheck disable=SC1090
    source "$config_file"
}

bb_resolve_profile_config() {
    local profile_id="$1"
    local explicit_config="$2"
    local profile_config_dir="$3"
    local legacy_config="$4"
    local profile_config

    if [[ -n "$explicit_config" ]]; then
        printf '%s\n' "$explicit_config"
        return 0
    fi

    bb_validate_safe_name PROFILE_ID "$profile_id"
    bb_validate_absolute_path PROFILE_CONFIG_DIR "$profile_config_dir"
    bb_validate_absolute_path LEGACY_CONFIG_FILE "$legacy_config"

    profile_config="$profile_config_dir/$profile_id.env"
    if [[ -e "$profile_config" ]]; then
        printf '%s\n' "$profile_config"
        return 0
    fi

    if [[ "$profile_id" == default && -e "$legacy_config" ]]; then
        bb_warn "Legacy configuration fallback is deprecated and will be removed in 2.0. Run: btrfs-backup-migrate-profile --profile default"
        printf '%s\n' "$legacy_config"
        return 0
    fi

    bb_die "Profile configuration does not exist: $profile_config"
}

bb_require_var() {
    local name="$1"
    if [[ -z "${!name:-}" ]]; then
        bb_die "Required configuration variable is empty: $name"
    fi
}

bb_validate_uint() {
    local name="$1"
    local value="$2"
    if [[ ! "$value" =~ ^(0|[1-9][0-9]*)$ ]]; then
        bb_die "$name must be a non-negative base-10 integer; got: $value"
    fi
}

bb_validate_positive_uint() {
    local name="$1"
    local value="$2"
    bb_validate_uint "$name" "$value"
    if (( 10#$value == 0 )); then
        bb_die "$name must be greater than zero."
    fi
}

bb_validate_safe_name() {
    local name="$1"
    local value="$2"
    if [[ ! "$value" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
        bb_die "$name contains unsupported characters: $value"
    fi
}

bb_validate_absolute_path() {
    local name="$1"
    local value="$2"
    if [[ "$value" != /* ]]; then
        bb_die "$name must be an absolute path; got: $value"
    fi
    if [[ "$value" == *$'\n'* || "$value" == *$'\r'* ]]; then
        bb_die "$name contains a newline."
    fi
}

bb_validate_relative_path() {
    local name="$1"
    local value="$2"
    if [[ -z "$value" || "$value" == /* ]]; then
        bb_die "$name must be a non-empty relative path; got: $value"
    fi
    case "/$value/" in
        */../*|*/./*) bb_die "$name must not contain . or .. path components: $value" ;;
    esac
    if [[ "$value" == *$'\n'* || "$value" == *$'\r'* ]]; then
        bb_die "$name contains a newline."
    fi
}

bb_realpath_m() {
    realpath -m -- "$1"
}

bb_path_is_within() {
    local candidate base
    candidate="$(bb_realpath_m "$1")"
    base="$(bb_realpath_m "$2")"
    [[ "$candidate" == "$base" || "$candidate" == "$base/"* ]]
}

bb_paths_are_same_filesystem() {
    local path_a="$1"
    local path_b="$2"
    local uuid_a uuid_b majmin_a majmin_b

    uuid_a="$(findmnt -n -o UUID -T "$path_a" 2>/dev/null || true)"
    uuid_b="$(findmnt -n -o UUID -T "$path_b" 2>/dev/null || true)"
    if [[ -n "$uuid_a" && -n "$uuid_b" ]]; then
        [[ "$uuid_a" == "$uuid_b" ]]
        return
    fi

    majmin_a="$(findmnt -n -o MAJ:MIN -T "$path_a" 2>/dev/null || true)"
    majmin_b="$(findmnt -n -o MAJ:MIN -T "$path_b" 2>/dev/null || true)"
    [[ -n "$majmin_a" && "$majmin_a" == "$majmin_b" ]]
}

bb_subvolume_field() {
    local path="$1"
    local field="$2"
    btrfs subvolume show "$path" 2>/dev/null \
        | sed -n "s/^[[:space:]]*${field}:[[:space:]]*//p" \
        | head -n1
}

bb_subvolume_uuid() {
    bb_subvolume_field "$1" UUID
}

bb_subvolume_received_uuid() {
    bb_subvolume_field "$1" 'Received UUID'
}

bb_is_subvolume() {
    btrfs subvolume show "$1" >/dev/null 2>&1
}

bb_is_readonly_subvolume() {
    local output
    output="$(btrfs property get -ts "$1" ro 2>/dev/null || true)"
    [[ "$output" == 'ro=true' ]]
}

bb_mount_source() {
    findmnt -n -o SOURCE -M "$1" 2>/dev/null | head -n1
}

bb_mount_fstype() {
    findmnt -n -o FSTYPE -M "$1" 2>/dev/null | head -n1
}

bb_mount_options() {
    findmnt -n -o OPTIONS -M "$1" 2>/dev/null | head -n1
}

bb_strip_subvolume_suffix() {
    local source="$1"
    printf '%s\n' "${source%%\[*}"
}

bb_canonical_device() {
    readlink -f -- "$1" 2>/dev/null || true
}

bb_mount_uses_mapper() {
    local mountpoint="$1"
    local mapper_name="$2"
    local actual expected

    actual="$(bb_mount_source "$mountpoint")"
    actual="$(bb_strip_subvolume_suffix "$actual")"
    expected="/dev/mapper/$mapper_name"

    actual="$(bb_canonical_device "$actual")"
    expected="$(bb_canonical_device "$expected")"
    [[ -n "$actual" && -n "$expected" && "$actual" == "$expected" ]]
}

bb_mapper_underlying_device() {
    local mapper_name="$1"
    cryptsetup status "$mapper_name" 2>/dev/null \
        | sed -n 's/^[[:space:]]*device:[[:space:]]*//p' \
        | head -n1
}

bb_available_bytes() {
    local output line
    output="$(df -B1 --output=avail -- "$1" 2>/dev/null)" || return
    line="${output##*$'\n'}"
    printf '%s\n' "${line//[[:space:]]/}"
}

bb_check_minimum_free_space() {
    local path="$1"
    local minimum="$2"
    local label="$3"
    local available

    bb_validate_uint "minimum free bytes for $label" "$minimum"
    (( 10#$minimum == 0 )) && return 0

    available="$(bb_available_bytes "$path")"
    if [[ ! "$available" =~ ^[0-9]+$ ]]; then
        bb_die "Could not determine free space for $label at $path"
    fi
    if (( 10#$available < 10#$minimum )); then
        bb_die "Insufficient free space for $label at $path: available=$available, required=$minimum"
    fi
}

bb_notify_desktop() {
    local title="$1"
    local message="$2"
    local notify_user="${NOTIFY_USER:-}"
    local notify_uid notify_bus

    command -v notify-send >/dev/null 2>&1 || return 1

    if [[ -n "$notify_user" && "$notify_user" != root ]] && command -v runuser >/dev/null 2>&1; then
        notify_uid="$(id -u "$notify_user" 2>/dev/null || true)"
        notify_bus="/run/user/${notify_uid}/bus"
        if [[ -n "$notify_uid" && -S "$notify_bus" ]]; then
            runuser -u "$notify_user" -- \
                env DBUS_SESSION_BUS_ADDRESS="unix:path=${notify_bus}" \
                notify-send "$title" "$message" >/dev/null 2>&1
            return $?
        fi
    fi

    if [[ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]]; then
        notify-send "$title" "$message" >/dev/null 2>&1
        return $?
    fi

    return 1
}

bb_notify() {
    local message="$1"
    local title="${2:-Btrfs backup}"
    local method="${NOTIFY_METHOD:-auto}"

    bb_log INFO "$message"

    if ! bb_bool_is_true "${NOTIFY_ENABLE:-true}"; then
        return 0
    fi

    case "$method" in
        none|journal)
            return 0
            ;;
        desktop)
            if ! bb_notify_desktop "$title" "$message"; then
                bb_warn "Desktop notification could not be delivered."
            fi
            ;;
        auto)
            bb_notify_desktop "$title" "$message" || true
            ;;
        *)
            bb_warn "Unknown NOTIFY_METHOD=$method; using journal only."
            ;;
    esac
}

bb_acquire_lock() {
    local lock_file="$1"
    local lock_dir

    lock_dir="$(dirname -- "$lock_file")"
    install -d -m0750 "$lock_dir"

    exec {BB_LOCK_FD}>"$lock_file"
    if ! flock -n "$BB_LOCK_FD"; then
        exec {BB_LOCK_FD}>&-
        unset BB_LOCK_FD
        return 1
    fi
}

bb_release_lock() {
    if [[ -n "${BB_LOCK_FD:-}" ]]; then
        flock -u "$BB_LOCK_FD" 2>/dev/null || true
        exec {BB_LOCK_FD}>&-
        unset BB_LOCK_FD
    fi
}

bb_list_direct_subvolumes() {
    local directory="$1"
    local entry

    [[ -d "$directory" ]] || return 0
    while IFS= read -r -d '' entry; do
        if bb_is_subvolume "$entry"; then
            printf '%s\n' "$entry"
        fi
    done < <(find "$directory" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z)
}

bb_delete_subvolume_or_path() {
    local path="$1"
    if bb_is_subvolume "$path"; then
        btrfs subvolume delete -- "$path"
    else
        rm -rf --one-file-system -- "$path"
    fi
}

bb_cleanup_directory_contents() {
    local directory="$1"
    local entry

    [[ -d "$directory" ]] || return 0
    while IFS= read -r -d '' entry; do
        bb_delete_subvolume_or_path "$entry"
    done < <(find "$directory" -mindepth 1 -maxdepth 1 -print0)
}
