#!/usr/bin/env bash
set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
INSTALLED_PREFIX_ROOT="$(realpath -m -- "$SCRIPT_DIR/..")"
INSTALLED_COMMON_LIB="$INSTALLED_PREFIX_ROOT/lib/btrfs-backup/lib/btrfs-backup-common.sh"
INSTALLED_EXAMPLES="$INSTALLED_PREFIX_ROOT/share/btrfs-backup/examples"

if [[ -r "$INSTALLED_COMMON_LIB" ]]; then
    # Works both after installation in /usr and from an extracted package tree.
    # shellcheck source=/usr/lib/btrfs-backup/lib/btrfs-backup-common.sh
    source "$INSTALLED_COMMON_LIB"
elif [[ -r /usr/lib/btrfs-backup/lib/btrfs-backup-common.sh ]]; then
    # Compatibility fallback for a non-standard launcher path.
    # shellcheck source=/usr/lib/btrfs-backup/lib/btrfs-backup-common.sh
    source /usr/lib/btrfs-backup/lib/btrfs-backup-common.sh
elif [[ -r "$REPO_ROOT/scripts/lib/btrfs-backup-common.sh" ]]; then
    # shellcheck source=../scripts/lib/btrfs-backup-common.sh
    source "$REPO_ROOT/scripts/lib/btrfs-backup-common.sh"
else
    printf '%s\n' 'Could not locate btrfs-backup-common.sh.' >&2
    exit 1
fi

ACTION=render
TEMPLATE_DIR=""
OUTPUT_DIR=""
ANSWERS_FILE=""
VALIDATE_DIR=""

usage() {
    cat <<'USAGE'
Usage: btrfs-backup-configure [options]

Actions:
  --render-only         Interactively render configuration files (default).
  --apply               Render and install active files under /etc.
  --validate            Validate the active installation without mounting the target.
  --validate-dir PATH   Validate a previously rendered directory.

Input/output:
  --answers PATH        Read non-interactive answers from a trusted shell file.
  --template-dir PATH   Accepted for compatibility; templates are rendered natively.
  --output-dir PATH     Override the rendered output directory.
  --cli                 Accepted for compatibility; the configurator is CLI-only.
  -h, --help            Show this help.

Runtime validation after installation:
  sudo btrfs-backup --validate
USAGE
}

while (( $# > 0 )); do
    case "$1" in
        --render-only)
            ACTION=render
            shift
            ;;
        --cli)
            shift
            ;;
        --apply)
            ACTION=apply
            shift
            ;;
        --validate)
            ACTION=validate
            shift
            ;;
        --validate-dir)
            [[ $# -ge 2 ]] || bb_die "--validate-dir requires a path."
            ACTION=validate-dir
            VALIDATE_DIR="$2"
            shift 2
            ;;
        --answers)
            [[ $# -ge 2 ]] || bb_die "--answers requires a path."
            ANSWERS_FILE="$2"
            shift 2
            ;;
        --template-dir)
            [[ $# -ge 2 ]] || bb_die "--template-dir requires a path."
            TEMPLATE_DIR="$2"
            shift 2
            ;;
        --output-dir)
            [[ $# -ge 2 ]] || bb_die "--output-dir requires a path."
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --gui)
            bb_die "The GUI configurator was removed. Use the deterministic CLI configurator."
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

if [[ -z "$TEMPLATE_DIR" ]]; then
    if [[ -d "$INSTALLED_EXAMPLES" ]]; then
        TEMPLATE_DIR="$INSTALLED_EXAMPLES"
    elif [[ -d /usr/share/btrfs-backup/examples ]]; then
        TEMPLATE_DIR=/usr/share/btrfs-backup/examples
    else
        TEMPLATE_DIR="$REPO_ROOT"
    fi
fi

if [[ -z "$OUTPUT_DIR" ]]; then
    if (( EUID == 0 )); then
        OUTPUT_DIR=/etc/btrfs-backup/generated
    else
        OUTPUT_DIR="$REPO_ROOT/generated"
    fi
fi

OUTPUT_DIR="$(realpath -m -- "$OUTPUT_DIR")"
case "$OUTPUT_DIR" in
    /|/etc|/usr|/var|/home|/root)
        bb_die "Refusing unsafe output directory: $OUTPUT_DIR"
        ;;
esac
if [[ "$OUTPUT_DIR" == "$(realpath -m -- "$REPO_ROOT")" ]]; then
    bb_die "The output directory must not be the repository root: $OUTPUT_DIR"
fi

# Rendering starts by removing OUTPUT_DIR. Refuse any directory that is or
# contains an active file managed by this project.
for protected_path in \
    /etc/btrfs-backup/backup.env \
    /etc/btrfs-backup/sources.d \
    /etc/systemd/system/btrfs-backup.service \
    /etc/udev/rules.d/99-btrfs-backup.rules; do
    if bb_path_is_within "$protected_path" "$OUTPUT_DIR"; then
        bb_die "Refusing output directory that contains active configuration: $OUTPUT_DIR"
    fi
done

udev_escape() {
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

prompt_value() {
    local variable_name="$1"
    local prompt_text="$2"
    local default_value="$3"
    local input

    read -r -p "$prompt_text [$default_value]: " input
    [[ -n "$input" ]] || input="$default_value"
    printf -v "$variable_name" '%s' "$input"
}

prompt_bool() {
    local variable_name="$1"
    local prompt_text="$2"
    local default_value="$3"
    local input

    while true; do
        read -r -p "$prompt_text [$default_value]: " input
        [[ -n "$input" ]] || input="$default_value"
        if bb_bool_is_true "$input"; then
            printf -v "$variable_name" '%s' true
            return
        fi
        if bb_bool_is_false "$input"; then
            printf -v "$variable_name" '%s' false
            return
        fi
        printf '%s\n' 'Enter true or false.' >&2
    done
}

get_udev_property() {
    local device="$1"
    local key="$2"
    local output

    output="$(udevadm info --query=property --name "$device" 2>/dev/null || true)"
    [[ -n "$output" ]] || return 0
    sed -n "s/^${key}=//p" <<< "$output" | head -n1
}

best_device_reference() {
    local device="$1"
    local luks_uuid="$2"
    local real_device candidate

    if [[ -n "$luks_uuid" && -e "/dev/disk/by-uuid/$luks_uuid" ]]; then
        printf '%s\n' "/dev/disk/by-uuid/$luks_uuid"
        return
    fi

    real_device="$(readlink -f "$device" 2>/dev/null || true)"
    if [[ -n "$real_device" && -d /dev/disk/by-id ]]; then
        while IFS= read -r candidate; do
            [[ -L "$candidate" ]] || continue
            [[ "$(readlink -f "$candidate" 2>/dev/null || true)" == "$real_device" ]] || continue
            [[ "$(basename -- "$candidate")" == dm-* ]] && continue
            printf '%s\n' "$candidate"
            return
        done < <(find /dev/disk/by-id -maxdepth 1 -type l | sort)
    fi

    printf '%s\n' "$device"
}

derive_udev_match() {
    local device="$1"
    local luks_uuid="$2"
    local lsblk_type="$3"
    local devtype fs_type fs_uuid part_uuid serial_short
    local conditions=()

    devtype="$(get_udev_property "$device" DEVTYPE)"
    fs_type="$(get_udev_property "$device" ID_FS_TYPE)"
    fs_uuid="$(get_udev_property "$device" ID_FS_UUID)"
    part_uuid="$(get_udev_property "$device" ID_PART_ENTRY_UUID)"
    serial_short="$(get_udev_property "$device" ID_SERIAL_SHORT)"

    [[ -n "$devtype" ]] || {
        if [[ "$lsblk_type" == part ]]; then devtype=partition; else devtype=disk; fi
    }
    [[ -n "$fs_type" ]] || fs_type=crypto_LUKS
    [[ -n "$fs_uuid" ]] || fs_uuid="$luks_uuid"

    [[ -n "$fs_uuid" ]] || bb_die "Could not determine the LUKS filesystem UUID for udev matching."

    conditions+=("ENV{DEVTYPE}==\"$(udev_escape "$devtype")\"")
    conditions+=("ENV{ID_FS_TYPE}==\"$(udev_escape "$fs_type")\"")
    conditions+=("ENV{ID_FS_UUID}==\"$(udev_escape "$fs_uuid")\"")
    [[ -n "$part_uuid" ]] && conditions+=("ENV{ID_PART_ENTRY_UUID}==\"$(udev_escape "$part_uuid")\"")
    [[ -n "$serial_short" ]] && conditions+=("ENV{ID_SERIAL_SHORT}==\"$(udev_escape "$serial_short")\"")

    local joined=""
    local condition
    for condition in "${conditions[@]}"; do
        [[ -z "$joined" ]] || joined+=", "
        joined+="$condition"
    done
    printf '%s\n' "$joined"
}

DEVICE_PATHS=()
DEVICE_TYPES=()
DEVICE_LABELS=()
DEVICE_UUIDS=()

build_device_candidates() {
    DEVICE_PATHS=()
    DEVICE_TYPES=()
    DEVICE_LABELS=()
    DEVICE_UUIDS=()

    local line key value rest
    local name type size fstype uuid rm hotplug tran model serial
    local pair_regex='^([A-Z0-9_]+)="([^"]*)"[[:space:]]*(.*)$'

    while IFS= read -r line; do
        name="" type="" size="" fstype="" uuid="" rm="" hotplug="" tran="" model="" serial=""
        rest="$line"
        while [[ "$rest" =~ $pair_regex ]]; do
            key="${BASH_REMATCH[1]}"
            value="${BASH_REMATCH[2]}"
            rest="${BASH_REMATCH[3]}"
            case "$key" in
                NAME) name="$value" ;;
                TYPE) type="$value" ;;
                SIZE) size="$value" ;;
                FSTYPE) fstype="$value" ;;
                UUID) uuid="$value" ;;
                RM) rm="$value" ;;
                HOTPLUG) hotplug="$value" ;;
                TRAN) tran="$value" ;;
                MODEL) model="$value" ;;
                SERIAL) serial="$value" ;;
            esac
        done

        [[ "$type" == part || "$type" == disk ]] || continue
        [[ "$fstype" == crypto_LUKS ]] || continue
        [[ -n "$uuid" ]] || uuid="$(blkid -s UUID -o value "$name" 2>/dev/null || true)"
        [[ -n "$uuid" ]] || continue

        local marker=""
        if [[ "$rm" == 1 || "$hotplug" == 1 || "$tran" == usb ]]; then
            marker='[removable] '
        fi
        DEVICE_PATHS+=("$name")
        DEVICE_TYPES+=("$type")
        DEVICE_UUIDS+=("$uuid")
        DEVICE_LABELS+=("${marker}${name} | ${size:-?} | ${tran:-?} | ${model:-?} | UUID=${uuid} | ${serial:-no-serial}")
    done < <(lsblk -P -p -o NAME,TYPE,SIZE,FSTYPE,UUID,RM,HOTPLUG,TRAN,MODEL,SERIAL)
}

select_backup_device() {
    build_device_candidates
    (( ${#DEVICE_PATHS[@]} > 0 )) || bb_die "No LUKS block devices were detected. Connect the backup target and try again."

    printf '%s\n' 'Detected LUKS devices:'
    local index
    for index in "${!DEVICE_PATHS[@]}"; do
        printf '%2d) %s\n' "$((index + 1))" "${DEVICE_LABELS[$index]}"
    done

    local choice
    while true; do
        read -r -p 'Select backup device [1]: ' choice
        [[ -n "$choice" ]] || choice=1
        if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#DEVICE_PATHS[@]} )); then
            local selected=$((choice - 1))
            BACKUP_SELECTED_DEVICE="${DEVICE_PATHS[$selected]}"
            BACKUP_SELECTED_DEVICE_TYPE="${DEVICE_TYPES[$selected]}"
            BACKUP_LUKS_UUID="${DEVICE_UUIDS[$selected]}"
            BACKUP_DEVICE="$(best_device_reference "$BACKUP_SELECTED_DEVICE" "$BACKUP_LUKS_UUID")"
            BACKUP_UDEV_MATCH="$(derive_udev_match "$BACKUP_SELECTED_DEVICE" "$BACKUP_LUKS_UUID" "$BACKUP_SELECTED_DEVICE_TYPE")"
            return
        fi
        printf '%s\n' 'Invalid selection.' >&2
    done
}

SOURCE_CANDIDATES=()

build_source_candidates() {
    mapfile -t SOURCE_CANDIDATES < <(findmnt -rn -t btrfs -o TARGET | sort -u)
}

source_default_selection() {
    local defaults=()
    local index
    for index in "${!SOURCE_CANDIDATES[@]}"; do
        case "${SOURCE_CANDIDATES[$index]}" in
            /|/home) defaults+=("$((index + 1))") ;;
        esac
    done
    if (( ${#defaults[@]} == 0 )); then
        printf '1'
    else
        local IFS=,
        printf '%s' "${defaults[*]}"
    fi
}

select_sources() {
    build_source_candidates
    (( ${#SOURCE_CANDIDATES[@]} > 0 )) || bb_die "No mounted Btrfs source subvolumes were detected."

    printf '\n%s\n' 'Detected mounted Btrfs sources:'
    local index
    for index in "${!SOURCE_CANDIDATES[@]}"; do
        printf '%2d) %s\n' "$((index + 1))" "${SOURCE_CANDIDATES[$index]}"
    done

    local default_selection selection token selected_index
    default_selection="$(source_default_selection)"
    read -r -p "Select one or more sources, comma-separated, or 'a' for all [$default_selection]: " selection
    [[ -n "$selection" ]] || selection="$default_selection"

    local selected_paths=()
    declare -A seen=()
    if [[ "$selection" == a || "$selection" == A ]]; then
        selected_paths=("${SOURCE_CANDIDATES[@]}")
    else
        IFS=',' read -r -a tokens <<< "$selection"
        for token in "${tokens[@]}"; do
            token="${token//[[:space:]]/}"
            [[ "$token" =~ ^[0-9]+$ ]] || bb_die "Invalid source selection: $token"
            selected_index=$((token - 1))
            (( selected_index >= 0 && selected_index < ${#SOURCE_CANDIDATES[@]} )) || bb_die "Source selection out of range: $token"
            [[ -n "${seen[$selected_index]+x}" ]] && continue
            seen["$selected_index"]=1
            selected_paths+=("${SOURCE_CANDIDATES[$selected_index]}")
        done
    fi

    (( ${#selected_paths[@]} > 0 )) || bb_die "No sources selected."
    SOURCE_SUBVOLUMES=("${selected_paths[@]}")
}

derive_source_name() {
    local path="$1"
    local name
    if [[ "$path" == / ]]; then
        name=root
    else
        name="$(basename -- "${path%/}")"
    fi
    name="${name//[^A-Za-z0-9._-]/-}"
    [[ "$name" =~ ^[A-Za-z0-9] ]] || name="source-$name"
    printf '%s\n' "$name"
}

collect_interactive_answers() {
    bb_require_commands blkid find findmnt lsblk sed sort systemd-escape udevadm
    select_backup_device

    prompt_value PROFILE_ID 'Profile identifier' default
    prompt_value PROFILE_NAME 'Profile display name' "$PROFILE_ID"
    prompt_value BACKUP_MAPPER_NAME 'LUKS mapper name' backupdisk
    prompt_value BACKUP_MOUNTPOINT 'Backup mountpoint' /mnt/backup

    local mapper_path="/dev/mapper/$BACKUP_MAPPER_NAME"
    local detected_btrfs_uuid=""
    if [[ -e "$mapper_path" ]]; then
        detected_btrfs_uuid="$(blkid -s UUID -o value "$mapper_path" 2>/dev/null || true)"
    fi
    prompt_value BACKUP_BTRFS_UUID 'Expected Btrfs UUID inside LUKS (empty disables this additional check)' "$detected_btrfs_uuid"

    select_sources

    SOURCE_NAMES=()
    LOCAL_SNAPSHOT_DIRS=()
    REMOTE_SUBDIRS=()
    SOURCE_RETENTION_COUNTS=()
    SOURCE_LOCAL_RETENTION_COUNTS=()

    local source default_name source_name local_dir remote_subdir
    declare -A used_names=()
    for source in "${SOURCE_SUBVOLUMES[@]}"; do
        default_name="$(derive_source_name "$source")"
        while [[ -n "${used_names[$default_name]+x}" ]]; do
            default_name="${default_name}-2"
        done
        prompt_value source_name "Source name for $source" "$default_name"
        bb_validate_safe_name SOURCE_NAME "$source_name"
        [[ -z "${used_names[$source_name]+x}" ]] || bb_die "Duplicate source name: $source_name"
        used_names["$source_name"]=1

        prompt_value local_dir "Local snapshot directory for $source" "/.snapshots/btrfs-backup/$source_name"
        prompt_value remote_subdir "Remote subdirectory under the backup snapshots root" "$source_name"

        SOURCE_NAMES+=("$source_name")
        LOCAL_SNAPSHOT_DIRS+=("$local_dir")
        REMOTE_SUBDIRS+=("$remote_subdir")
        SOURCE_RETENTION_COUNTS+=("")
        SOURCE_LOCAL_RETENTION_COUNTS+=("")
    done

    prompt_value RETENTION_COUNT 'Remote retention count; 0 means unlimited' 30
    prompt_value LOCAL_RETENTION_COUNT 'Local retention count; 0 means unlimited' 30
    prompt_bool DAILY_LIMIT 'Run at most once per local calendar day' true
    prompt_bool INCREMENTAL_REQUIRED 'Fail instead of silently starting a new full chain when remote snapshots exist' true
    prompt_bool KEEP_FAILED_LOCAL_SNAPSHOT 'Keep a new local snapshot after a failed transfer' false
    prompt_bool AUTO_EJECT 'Unmount and close LUKS automatically after the service finishes' true
    prompt_value MIN_TARGET_FREE_BYTES 'Minimum free bytes required on the backup target; 0 disables' 5368709120
    prompt_value MIN_LOCAL_FREE_BYTES 'Minimum free bytes required for local snapshots; 0 disables' 1073741824
    prompt_value KEYFILE_PATH_OR_NONE 'crypttab keyfile path or none' "/root/keys/${BACKUP_MAPPER_NAME}.key"

    prompt_bool NOTIFY_ENABLE 'Enable notifications' true
    prompt_value NOTIFY_USER 'Desktop notification user' "${SUDO_USER:-${USER:-root}}"
    prompt_value NOTIFY_METHOD 'Notification method: auto, desktop, journal, or none' auto
}

assert_private_shell_input() {
    local path="$1"
    local owner_uid mode permissions expected_uid

    [[ -f "$path" ]] || bb_die "Shell input file does not exist or is not a regular file: $path"
    owner_uid="$(stat -Lc '%u' -- "$path" 2>/dev/null || true)"
    mode="$(stat -Lc '%a' -- "$path" 2>/dev/null || true)"
    expected_uid="${SUDO_UID:-$EUID}"
    [[ "$expected_uid" =~ ^[0-9]+$ ]] || bb_die "Could not determine the invoking user UID."
    [[ "$owner_uid" == "$expected_uid" ]] \
        || bb_die "Shell input must be owned by the invoking user (uid $expected_uid): $path"
    [[ "$mode" =~ ^[0-7]{3,4}$ ]] || bb_die "Could not determine permissions for: $path"
    permissions=$((8#$mode))
    (( (permissions & 0077) == 0 )) || bb_die "Shell input must be private (mode 0600 recommended): $path (mode $mode)"
}

load_answers_file() {
    assert_private_shell_input "$ANSWERS_FILE"
    # Trusted local deployment input.
    # shellcheck disable=SC1090
    source "$ANSWERS_FILE"

    declare -p SOURCE_SUBVOLUMES >/dev/null 2>&1 || SOURCE_SUBVOLUMES=()
    declare -p SOURCE_NAMES >/dev/null 2>&1 || SOURCE_NAMES=()
    declare -p LOCAL_SNAPSHOT_DIRS >/dev/null 2>&1 || LOCAL_SNAPSHOT_DIRS=()
    declare -p REMOTE_SUBDIRS >/dev/null 2>&1 || REMOTE_SUBDIRS=()
    declare -p SOURCE_RETENTION_COUNTS >/dev/null 2>&1 || SOURCE_RETENTION_COUNTS=()
    declare -p SOURCE_LOCAL_RETENTION_COUNTS >/dev/null 2>&1 || SOURCE_LOCAL_RETENTION_COUNTS=()

    PROFILE_ID="${PROFILE_ID:-default}"
    PROFILE_NAME="${PROFILE_NAME:-$PROFILE_ID}"
    BACKUP_MAPPER_NAME="${BACKUP_MAPPER_NAME:-backupdisk}"
    BACKUP_MOUNTPOINT="${BACKUP_MOUNTPOINT:-/mnt/backup}"
    BACKUP_BTRFS_UUID="${BACKUP_BTRFS_UUID:-}"
    RETENTION_COUNT="${RETENTION_COUNT:-30}"
    LOCAL_RETENTION_COUNT="${LOCAL_RETENTION_COUNT:-30}"
    DAILY_LIMIT="${DAILY_LIMIT:-true}"
    INCREMENTAL_REQUIRED="${INCREMENTAL_REQUIRED:-true}"
    KEEP_FAILED_LOCAL_SNAPSHOT="${KEEP_FAILED_LOCAL_SNAPSHOT:-false}"
    AUTO_EJECT="${AUTO_EJECT:-true}"
    MIN_TARGET_FREE_BYTES="${MIN_TARGET_FREE_BYTES:-5368709120}"
    MIN_LOCAL_FREE_BYTES="${MIN_LOCAL_FREE_BYTES:-1073741824}"
    KEYFILE_PATH_OR_NONE="${KEYFILE_PATH_OR_NONE:-none}"
    NOTIFY_ENABLE="${NOTIFY_ENABLE:-true}"
    NOTIFY_USER="${NOTIFY_USER:-root}"
    NOTIFY_METHOD="${NOTIFY_METHOD:-auto}"

    bb_require_var BACKUP_DEVICE
    bb_require_var BACKUP_LUKS_UUID
    if [[ -z "${BACKUP_UDEV_MATCH:-}" ]]; then
        local device_for_probe="$BACKUP_DEVICE"
        [[ -e "$device_for_probe" ]] || bb_die "BACKUP_UDEV_MATCH is required when BACKUP_DEVICE cannot be probed."
        local detected_type
        detected_type="$(lsblk -no TYPE "$device_for_probe" 2>/dev/null | head -n1)"
        BACKUP_UDEV_MATCH="$(derive_udev_match "$device_for_probe" "$BACKUP_LUKS_UUID" "$detected_type")"
    fi

    local source_count="${#SOURCE_SUBVOLUMES[@]}"
    (( source_count > 0 )) || bb_die "The answers file must define at least one SOURCE_SUBVOLUMES entry."
    (( ${#SOURCE_NAMES[@]} == source_count )) || bb_die "SOURCE_NAMES length does not match SOURCE_SUBVOLUMES."
    (( ${#LOCAL_SNAPSHOT_DIRS[@]} == source_count )) || bb_die "LOCAL_SNAPSHOT_DIRS length does not match SOURCE_SUBVOLUMES."
    (( ${#REMOTE_SUBDIRS[@]} == source_count )) || bb_die "REMOTE_SUBDIRS length does not match SOURCE_SUBVOLUMES."

    while (( ${#SOURCE_RETENTION_COUNTS[@]} < source_count )); do SOURCE_RETENTION_COUNTS+=(""); done
    while (( ${#SOURCE_LOCAL_RETENTION_COUNTS[@]} < source_count )); do SOURCE_LOCAL_RETENTION_COUNTS+=(""); done
}

detect_runtime_script() {
    local filename="$1"
    local prefixed_path="$INSTALLED_PREFIX_ROOT/lib/btrfs-backup/$filename"
    if [[ -x "$prefixed_path" ]]; then
        printf '%s\n' "$prefixed_path"
    elif [[ -x "/usr/lib/btrfs-backup/$filename" ]]; then
        printf '%s\n' "/usr/lib/btrfs-backup/$filename"
    elif [[ -x "$REPO_ROOT/scripts/$filename" ]]; then
        printf '%s\n' "$REPO_ROOT/scripts/$filename"
    else
        printf '%s\n' "/usr/lib/btrfs-backup/$filename"
    fi
}

detect_profile_helper() {
    if [[ -x "$REPO_ROOT/bin/btrfs-backupctl" ]]; then
        printf '%s\n' "$REPO_ROOT/bin/btrfs-backupctl"
    elif [[ -x "$INSTALLED_PREFIX_ROOT/bin/btrfs-backupctl" ]]; then
        printf '%s\n' "$INSTALLED_PREFIX_ROOT/bin/btrfs-backupctl"
    elif [[ -x /usr/bin/btrfs-backupctl ]]; then
        printf '%s\n' /usr/bin/btrfs-backupctl
    else
        bb_die "Could not locate btrfs-backupctl."
    fi
}

extract_udev_env_match() {
    local key="$1"
    local pattern="ENV\\{$key\\}==\\\"([^\\\"]*)\\\""
    if [[ "$BACKUP_UDEV_MATCH" =~ $pattern ]]; then
        printf '%s\n' "${BASH_REMATCH[1]}"
    fi
}

write_profile_json() {
    local destination="$1"
    local source_count="${#SOURCE_SUBVOLUMES[@]}"
    local index source_retention source_local_retention
    local partition_uuid serial
    local args

    partition_uuid="$(extract_udev_env_match ID_PART_ENTRY_UUID)"
    serial="$(extract_udev_env_match ID_SERIAL_SHORT)"
    args=(
        profile create
        --output "$destination"
        --profile "$PROFILE_ID"
        --name "$PROFILE_NAME"
        --device "$BACKUP_DEVICE"
        --luks-uuid "$BACKUP_LUKS_UUID"
        --btrfs-uuid "$BACKUP_BTRFS_UUID"
        --partition-uuid "$partition_uuid"
        --serial "$serial"
        --mapper-name "$BACKUP_MAPPER_NAME"
        --mount-point "$BACKUP_MOUNTPOINT"
        --remote-root "$REMOTE_ROOT"
        --incoming-root "$INCOMING_ROOT"
        --remote-retention "$RETENTION_COUNT"
        --local-retention "$LOCAL_RETENTION_COUNT"
        --daily-limit "$DAILY_LIMIT"
        --incremental-required "$INCREMENTAL_REQUIRED"
        --keep-failed-local-snapshot "$KEEP_FAILED_LOCAL_SNAPSHOT"
        --auto-eject "$AUTO_EJECT"
        --minimum-target-free-bytes "$MIN_TARGET_FREE_BYTES"
        --minimum-local-free-bytes "$MIN_LOCAL_FREE_BYTES"
        --notify-enable "$NOTIFY_ENABLE"
        --notify-user "$NOTIFY_USER"
        --notify-method "$NOTIFY_METHOD"
    )
    for ((index = 0; index < source_count; index++)); do
        source_retention="${SOURCE_RETENTION_COUNTS[$index]:-$RETENTION_COUNT}"
        source_local_retention="${SOURCE_LOCAL_RETENTION_COUNTS[$index]:-$LOCAL_RETENTION_COUNT}"
        bb_validate_uint SOURCE_RETENTION_COUNT "$source_retention"
        bb_validate_uint SOURCE_LOCAL_RETENTION_COUNT "$source_local_retention"
        args+=(
            --source
            "${SOURCE_NAMES[$index]}"
            "${SOURCE_NAMES[$index]}"
            "${SOURCE_SUBVOLUMES[$index]}"
            "${LOCAL_SNAPSHOT_DIRS[$index]}"
            "${REMOTE_SUBDIRS[$index]}"
            "$source_retention"
            "$source_local_retention"
        )
    done
    "$PROFILE_HELPER" "${args[@]}"
}

validate_uuid() {
    local name="$1"
    local value="$2"
    local allow_empty="${3:-false}"

    if [[ -z "$value" ]] && bb_bool_is_true "$allow_empty"; then
        return 0
    fi
    if [[ ! "$value" =~ ^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$ ]]; then
        bb_die "$name must be a canonical UUID; got: $value"
    fi
}

validate_answers() {
    bb_validate_safe_name PROFILE_ID "$PROFILE_ID"
    [[ "$PROFILE_NAME" != *$'\n'* && "$PROFILE_NAME" != *$'\r'* && "$PROFILE_NAME" != *$'\t'* ]] \
        || bb_die "PROFILE_NAME must be a single-line value without tabs."
    bb_validate_safe_name BACKUP_MAPPER_NAME "$BACKUP_MAPPER_NAME"
    bb_validate_absolute_path BACKUP_DEVICE "$BACKUP_DEVICE"
    bb_validate_absolute_path BACKUP_MOUNTPOINT "$BACKUP_MOUNTPOINT"
    validate_uuid BACKUP_LUKS_UUID "$BACKUP_LUKS_UUID"
    validate_uuid BACKUP_BTRFS_UUID "$BACKUP_BTRFS_UUID" true
    [[ "$BACKUP_UDEV_MATCH" != *$'\n'* && "$BACKUP_UDEV_MATCH" != *$'\r'* ]] \
        || bb_die "BACKUP_UDEV_MATCH must be a single udev rule condition list."
    [[ "$BACKUP_UDEV_MATCH" == *'ENV{ID_FS_UUID}=='* ]] \
        || bb_die "BACKUP_UDEV_MATCH must match the LUKS filesystem UUID."
    local expected_uuid_match="ENV{ID_FS_UUID}==\"${BACKUP_LUKS_UUID}\""
    [[ "${BACKUP_UDEV_MATCH,,}" == *"${expected_uuid_match,,}"* ]] \
        || bb_die "BACKUP_UDEV_MATCH must contain the configured BACKUP_LUKS_UUID."
    if [[ "$KEYFILE_PATH_OR_NONE" != none ]]; then
        bb_validate_absolute_path KEYFILE_PATH_OR_NONE "$KEYFILE_PATH_OR_NONE"
    fi
    bb_validate_uint RETENTION_COUNT "$RETENTION_COUNT"
    bb_validate_uint LOCAL_RETENTION_COUNT "$LOCAL_RETENTION_COUNT"
    bb_validate_uint MIN_TARGET_FREE_BYTES "$MIN_TARGET_FREE_BYTES"
    bb_validate_uint MIN_LOCAL_FREE_BYTES "$MIN_LOCAL_FREE_BYTES"
    bb_validate_bool DAILY_LIMIT "$DAILY_LIMIT"
    bb_validate_bool INCREMENTAL_REQUIRED "$INCREMENTAL_REQUIRED"
    bb_validate_bool KEEP_FAILED_LOCAL_SNAPSHOT "$KEEP_FAILED_LOCAL_SNAPSHOT"
    bb_validate_bool AUTO_EJECT "$AUTO_EJECT"
    bb_validate_bool NOTIFY_ENABLE "$NOTIFY_ENABLE"
    case "$NOTIFY_METHOD" in auto|desktop|journal|none) ;; *) bb_die "Invalid NOTIFY_METHOD=$NOTIFY_METHOD" ;; esac

    local count="${#SOURCE_SUBVOLUMES[@]}"
    local index
    declare -A seen=()
    for ((index = 0; index < count; index++)); do
        bb_validate_safe_name SOURCE_NAME "${SOURCE_NAMES[$index]}"
        [[ -z "${seen[${SOURCE_NAMES[$index]}]+x}" ]] || bb_die "Duplicate source name: ${SOURCE_NAMES[$index]}"
        seen["${SOURCE_NAMES[$index]}"]=1
        bb_validate_absolute_path SOURCE_SUBVOLUME "${SOURCE_SUBVOLUMES[$index]}"
        bb_validate_absolute_path LOCAL_SNAPSHOT_DIR "${LOCAL_SNAPSHOT_DIRS[$index]}"
        bb_validate_relative_path REMOTE_SUBDIR "${REMOTE_SUBDIRS[$index]}"
        [[ "${SOURCE_SUBVOLUMES[$index]}" != *$'\t'* ]] || bb_die "SOURCE_SUBVOLUME must not contain tabs."
        [[ "${LOCAL_SNAPSHOT_DIRS[$index]}" != *$'\t'* ]] || bb_die "LOCAL_SNAPSHOT_DIR must not contain tabs."
        [[ "${REMOTE_SUBDIRS[$index]}" != *$'\t'* ]] || bb_die "REMOTE_SUBDIR must not contain tabs."
    done
}

validate_rendered_tree() {
    local root="$1"
    local profile_helper
    profile_helper="${PROFILE_HELPER:-$(detect_profile_helper)}"
    "$profile_helper" installation validate --rendered-root "$root"
}

validate_active_installation() {
    local profile_id="${PROFILE_ID:-default}"
    local profile_helper
    profile_helper="${PROFILE_HELPER:-$(detect_profile_helper)}"
    "$profile_helper" installation validate --active --profile "$profile_id"
}

if [[ "$ACTION" == validate ]]; then
    bb_require_commands systemd-analyze systemd-escape udevadm
    validate_active_installation
    exit 0
fi

if [[ "$ACTION" == validate-dir ]]; then
    bb_require_commands systemd-analyze udevadm
    validate_rendered_tree "$VALIDATE_DIR"
    exit 0
fi

if [[ -n "$ANSWERS_FILE" ]]; then
    load_answers_file
else
    collect_interactive_answers
fi
validate_answers

BACKUP_MOUNT_UNIT="$(systemd-escape -p --suffix=mount "$BACKUP_MOUNTPOINT")"
BACKUP_SCRIPT_PATH="$(detect_runtime_script btrfs-backup.sh)"
EJECT_SCRIPT_PATH="$(detect_runtime_script btrfs-backup-eject.sh)"
PROFILE_HELPER="$(detect_profile_helper)"
REMOTE_ROOT="$BACKUP_MOUNTPOINT/snapshots"
INCOMING_ROOT="$BACKUP_MOUNTPOINT/.incoming"

BACKUP_BTRFS_UUID="${BACKUP_BTRFS_UUID:-}"
KEYFILE_PATH_OR_NONE="${KEYFILE_PATH_OR_NONE:-none}"

rm -rf -- "$OUTPUT_DIR"
install -d -m0750 "$OUTPUT_DIR/config" "$OUTPUT_DIR/systemd" "$OUTPUT_DIR/udev"
write_profile_json "$OUTPUT_DIR/config/profile.json"
"$PROFILE_HELPER" \
    profile \
    --etc-root "$OUTPUT_DIR/config" \
    --udev-root "$OUTPUT_DIR/udev" \
    --public-root "$OUTPUT_DIR/public/profiles" \
    save --file "$OUTPUT_DIR/config/profile.json" >/dev/null
install -m0644 "$OUTPUT_DIR/udev/99-btrfs-backup-$PROFILE_ID.rules" "$OUTPUT_DIR/udev/99-btrfs-backup.rules"
"$PROFILE_HELPER" \
    installation \
    render \
    --file "$OUTPUT_DIR/config/profile.json" \
    --output-dir "$OUTPUT_DIR" \
    --backup-script "$BACKUP_SCRIPT_PATH" \
    --eject-script "$EJECT_SCRIPT_PATH" \
    --keyfile "$KEYFILE_PATH_OR_NONE"

chmod 0600 "$OUTPUT_DIR/config/profiles/$PROFILE_ID/profile.json"
validate_rendered_tree "$OUTPUT_DIR"

if [[ "$ACTION" == apply ]]; then
    bb_require_root
    bb_require_commands cp flock stat

    if ! bb_acquire_lock /run/btrfs-backup/backup.lock; then
        bb_die "A backup or eject operation is running; refusing to replace its configuration."
    fi
    trap bb_release_lock EXIT

    install -d -m0700 /etc/btrfs-backup "$BACKUP_MOUNTPOINT"
    migration_dir="/etc/btrfs-backup/migration-$(date +%Y%m%dT%H%M%S)-$$"
    install -d -m0700 "$migration_dir"

    backup_existing_file() {
        local path="$1"
        local backup_name="$2"
        [[ -e "$path" ]] || return 0
        if [[ -d "$path" ]]; then
            cp -a -- "$path" "$migration_dir/$backup_name"
            chmod -R go-rwx "$migration_dir/$backup_name"
        else
            install -m0600 "$path" "$migration_dir/$backup_name"
        fi
    }

    backup_existing_file /etc/btrfs-backup/backup.env backup.env
    backup_existing_file /etc/btrfs-backup/profiles.d profiles.d
    backup_existing_file /etc/btrfs-backup/sources.d sources.d
    backup_existing_file "/etc/btrfs-backup/profiles/$PROFILE_ID" "profile-$PROFILE_ID"
    backup_existing_file /etc/systemd/system/btrfs-backup.service btrfs-backup.service
    backup_existing_file /etc/systemd/system/btrfs-backup@.service 'btrfs-backup@.service'
    backup_existing_file /etc/udev/rules.d/99-btrfs-backup.rules 99-btrfs-backup.rules

    install -d -m0700 "/etc/btrfs-backup/profiles/$PROFILE_ID"
    rm -rf -- "/etc/btrfs-backup/profiles/$PROFILE_ID/sources.d"
    install -Dm0600 "$OUTPUT_DIR/config/profile.json" "/etc/btrfs-backup/profiles/$PROFILE_ID/profile.json"
    install -Dm0644 "$OUTPUT_DIR/public/profiles/$PROFILE_ID.json" "/var/lib/btrfs-backup/public/profiles/$PROFILE_ID.json"

    install -Dm0644 "$OUTPUT_DIR/systemd/btrfs-backup.service" /etc/systemd/system/.btrfs-backup.service.new
    mv -f -- /etc/systemd/system/.btrfs-backup.service.new /etc/systemd/system/btrfs-backup.service
    install -Dm0644 "$OUTPUT_DIR/systemd/btrfs-backup@.service" "/etc/systemd/system/.btrfs-backup@.service.new"
    mv -f -- "/etc/systemd/system/.btrfs-backup@.service.new" "/etc/systemd/system/btrfs-backup@.service"
    install -Dm0644 "$OUTPUT_DIR/udev/99-btrfs-backup.rules" /etc/udev/rules.d/.99-btrfs-backup.rules.new
    mv -f -- /etc/udev/rules.d/.99-btrfs-backup.rules.new /etc/udev/rules.d/99-btrfs-backup.rules
    rm -f -- /etc/btrfs-backup/backup.env
    rm -rf -- /etc/btrfs-backup/profiles.d

    migrate_old_dropin() {
        local path="$1"
        local backup_name="$2"
        [[ -f "$path" ]] || return 0
        install -m0600 "$path" "$migration_dir/$backup_name"
        rm -f "$path"
        bb_log INFO "Removed obsolete mount drop-in: $path"
    }

    old_dropin="/etc/systemd/system/${BACKUP_MOUNT_UNIT}.d/backup.conf"
    if [[ -f "$old_dropin" ]] && grep -Eq '(^|[[:space:]])(Wants|After)=.*btrfs-backup\.service' "$old_dropin"; then
        migrate_old_dropin "$old_dropin" obsolete-mount-backup.conf
    fi

    old_crypt_dropin="/etc/systemd/system/${BACKUP_MOUNT_UNIT}.d/crypt.conf"
    if [[ -f "$old_crypt_dropin" ]]         && grep -q '^Requires=systemd-cryptsetup@' "$old_crypt_dropin"         && grep -q '^After=systemd-cryptsetup@' "$old_crypt_dropin"; then
        migrate_old_dropin "$old_crypt_dropin" obsolete-mount-crypt.conf
    fi
    rmdir "/etc/systemd/system/${BACKUP_MOUNT_UNIT}.d" 2>/dev/null || true
    bb_log INFO "Previous active files and removed legacy drop-ins were saved under $migration_dir"

    systemctl disable btrfs-backup.service >/dev/null 2>&1 || true
    systemctl daemon-reload
    udevadm control --reload-rules
    validate_active_installation
    bb_release_lock
    trap - EXIT

    cat <<MSG
Installed active configuration:
  /etc/btrfs-backup/profiles/$PROFILE_ID/profile.json
  /var/lib/btrfs-backup/public/profiles/$PROFILE_ID.json
  /etc/systemd/system/btrfs-backup.service
  /etc/systemd/system/btrfs-backup@.service
  /etc/udev/rules.d/99-btrfs-backup.rules

Manual merge still required:
  $OUTPUT_DIR/config/crypttab.fragment -> /etc/crypttab
  $OUTPUT_DIR/config/fstab.fragment    -> /etc/fstab

After merging and reloading systemd, connect the target and run:
  sudo btrfs-backup --validate
MSG
else
    cat <<MSG
Rendered and validated files in:
  $OUTPUT_DIR

Review them, merge the fstab/crypttab fragments manually, or rerun with --apply as root.
MSG
fi
