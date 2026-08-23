#!/usr/bin/env bash
set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/btrfs-backup-common.sh
source "$SCRIPT_DIR/lib/btrfs-backup-common.sh"

REQUESTED_PROFILE_ID="${BTRFS_BACKUP_PROFILE:-default}"
PROFILE_WAS_REQUESTED=0
[[ -n "${BTRFS_BACKUP_PROFILE:-}" ]] && PROFILE_WAS_REQUESTED=1
PROFILE_CONFIG_DIR="${BTRFS_BACKUP_PROFILE_CONFIG_DIR:-/etc/btrfs-backup/profiles.d}"
PROFILE_JSON_FILE="${BTRFS_BACKUP_PROFILE_JSON:-}"
FORCE_RUN=0
VALIDATE_ONLY=0
NO_EJECT=0
CURRENT_STEP="initialization"
CURRENT_LOCAL_SNAPSHOT=""
CURRENT_INCOMING_RUN_DIR=""
CURRENT_RECEIVED_SUBVOLUME=""
CURRENT_PENDING_MARKER=""
CURRENT_REMOTE_SNAPSHOT_DIR=""
RUN_SUCCEEDED=0
RUN_SKIPPED=0
SOURCE_COUNT=0
PROFILE_STATE_DIR=""
RUN_STARTED_AT=""
RUN_FINISHED_AT=""
CURRENT_PHASE="initialization"
CURRENT_MESSAGE=""
CURRENT_SOURCE_NAME=""
SOURCE_INDEX=0

usage() {
    cat <<'USAGE'
Usage: btrfs-backup [options]

Options:
  --profile ID   Use /etc/btrfs-backup/profiles/ID/profile.json.
  --force         Run even if a successful backup was already made today.
  --validate      Mount the target and validate configuration without creating snapshots.
  --no-eject      Do not automatically eject after a manual invocation.
  -h, --help      Show this help.
USAGE
}

while (( $# > 0 )); do
    case "$1" in
        --profile)
            [[ $# -ge 2 ]] || bb_die "--profile requires an identifier."
            REQUESTED_PROFILE_ID="$2"
            PROFILE_WAS_REQUESTED=1
            shift 2
            ;;
        --force)
            FORCE_RUN=1
            shift
            ;;
        --validate)
            VALIDATE_ONLY=1
            shift
            ;;
        --no-eject)
            NO_EJECT=1
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

load_main_config() {
    PROFILE_JSON_FILE="$(bb_resolve_profile_json "$REQUESTED_PROFILE_ID" "$PROFILE_JSON_FILE" "$PROFILE_CONFIG_DIR")"
    bb_load_profile_json_config "$PROFILE_JSON_FILE"

    PROFILE_ID="${PROFILE_ID:-$REQUESTED_PROFILE_ID}"
    if (( PROFILE_WAS_REQUESTED == 1 )) && [[ "$PROFILE_ID" != "$REQUESTED_PROFILE_ID" ]]; then
        bb_die "Requested profile $REQUESTED_PROFILE_ID but loaded profile declares PROFILE_ID=$PROFILE_ID"
    fi
    PROFILE_NAME="${PROFILE_NAME:-$PROFILE_ID}"
    BACKUP_MOUNT_UNIT="${BACKUP_MOUNT_UNIT:-}"
    BACKUP_SERVICE_NAME="${BACKUP_SERVICE_NAME:-btrfs-backup.service}"
    BACKUP_BTRFS_UUID="${BACKUP_BTRFS_UUID:-}"
    SOURCES_DIR="${SOURCES_DIR:-/etc/btrfs-backup/sources.d}"
    REMOTE_ROOT="${REMOTE_ROOT:-${BACKUP_MOUNTPOINT:-}/snapshots}"
    INCOMING_ROOT="${INCOMING_ROOT:-${BACKUP_MOUNTPOINT:-}/.incoming}"
    RETENTION_COUNT="${RETENTION_COUNT:-30}"
    LOCAL_RETENTION_COUNT="${LOCAL_RETENTION_COUNT:-$RETENTION_COUNT}"
    DAILY_LIMIT="${DAILY_LIMIT:-true}"
    INCREMENTAL_REQUIRED="${INCREMENTAL_REQUIRED:-true}"
    KEEP_FAILED_LOCAL_SNAPSHOT="${KEEP_FAILED_LOCAL_SNAPSHOT:-false}"
    AUTO_EJECT="${AUTO_EJECT:-true}"
    NOTIFY_ENABLE="${NOTIFY_ENABLE:-true}"
    NOTIFY_METHOD="${NOTIFY_METHOD:-auto}"
    NOTIFY_USER="${NOTIFY_USER:-}"
    LOCK_FILE="${BTRFS_BACKUP_LOCK_FILE:-${LOCK_FILE:-/run/btrfs-backup/backup.lock}}"
    STATE_DIR="${STATE_DIR:-/var/lib/btrfs-backup}"
    STATUS_ROOT="${STATUS_ROOT:-/run/btrfs-backup/profiles}"
    HISTORY_ROOT="${HISTORY_ROOT:-$STATE_DIR/history}"
    MIN_TARGET_FREE_BYTES="${MIN_TARGET_FREE_BYTES:-5368709120}"
    MIN_LOCAL_FREE_BYTES="${MIN_LOCAL_FREE_BYTES:-1073741824}"
    EJECT_SCRIPT_PATH="${EJECT_SCRIPT_PATH:-$SCRIPT_DIR/btrfs-backup-eject.sh}"

    local required
    for required in \
        PROFILE_ID PROFILE_NAME BACKUP_MAPPER_NAME BACKUP_MOUNTPOINT BACKUP_MOUNT_UNIT \
        BACKUP_DEVICE BACKUP_LUKS_UUID SOURCES_DIR REMOTE_ROOT \
        INCOMING_ROOT LOCK_FILE STATE_DIR STATUS_ROOT HISTORY_ROOT; do
        bb_require_var "$required"
    done

    bb_validate_safe_name PROFILE_ID "$PROFILE_ID"
    bb_validate_safe_name BACKUP_MAPPER_NAME "$BACKUP_MAPPER_NAME"
    bb_validate_systemd_service_name BACKUP_SERVICE_NAME "$BACKUP_SERVICE_NAME"
    bb_validate_absolute_path BACKUP_MOUNTPOINT "$BACKUP_MOUNTPOINT"
    bb_validate_absolute_path BACKUP_DEVICE "$BACKUP_DEVICE"
    bb_validate_absolute_path SOURCES_DIR "$SOURCES_DIR"
    bb_validate_absolute_path REMOTE_ROOT "$REMOTE_ROOT"
    bb_validate_absolute_path INCOMING_ROOT "$INCOMING_ROOT"
    bb_validate_absolute_path LOCK_FILE "$LOCK_FILE"
    bb_validate_absolute_path STATE_DIR "$STATE_DIR"
    bb_validate_absolute_path STATUS_ROOT "$STATUS_ROOT"
    bb_validate_absolute_path HISTORY_ROOT "$HISTORY_ROOT"
    bb_validate_absolute_path EJECT_SCRIPT_PATH "$EJECT_SCRIPT_PATH"
    bb_validate_absolute_path PROFILE_CONFIG_DIR "$PROFILE_CONFIG_DIR"
    bb_validate_uint RETENTION_COUNT "$RETENTION_COUNT"
    bb_validate_uint LOCAL_RETENTION_COUNT "$LOCAL_RETENTION_COUNT"
    bb_validate_uint MIN_TARGET_FREE_BYTES "$MIN_TARGET_FREE_BYTES"
    bb_validate_uint MIN_LOCAL_FREE_BYTES "$MIN_LOCAL_FREE_BYTES"
    bb_validate_bool DAILY_LIMIT "$DAILY_LIMIT"
    bb_validate_bool INCREMENTAL_REQUIRED "$INCREMENTAL_REQUIRED"
    bb_validate_bool KEEP_FAILED_LOCAL_SNAPSHOT "$KEEP_FAILED_LOCAL_SNAPSHOT"
    bb_validate_bool AUTO_EJECT "$AUTO_EJECT"
    bb_validate_bool NOTIFY_ENABLE "$NOTIFY_ENABLE"

    case "$NOTIFY_METHOD" in
        auto|desktop|journal|none) ;;
        *) bb_die "NOTIFY_METHOD must be auto, desktop, journal, or none." ;;
    esac

    if [[ "$REMOTE_ROOT" == "$INCOMING_ROOT" ]] \
        || bb_path_is_within "$REMOTE_ROOT" "$INCOMING_ROOT" \
        || bb_path_is_within "$INCOMING_ROOT" "$REMOTE_ROOT"; then
        bb_die "REMOTE_ROOT and INCOMING_ROOT must be separate, non-nested directories."
    fi

    local expected_mount_unit
    expected_mount_unit="$(systemd-escape -p --suffix=mount "$BACKUP_MOUNTPOINT")"
    if [[ "$BACKUP_MOUNT_UNIT" != "$expected_mount_unit" ]]; then
        bb_die "BACKUP_MOUNT_UNIT=$BACKUP_MOUNT_UNIT does not match $BACKUP_MOUNTPOINT (expected $expected_mount_unit)."
    fi

    if [[ ! -x "$EJECT_SCRIPT_PATH" ]]; then
        bb_die "Eject script is missing or not executable: $EJECT_SCRIPT_PATH"
    fi

    PROFILE_STATE_DIR="$STATE_DIR/profiles/$PROFILE_ID"
    PROFILE_JSON_FILE="$(realpath -m -- "$PROFILE_JSON_FILE")"
}

backupctl_path() {
    local candidate
    for candidate in \
        "$SCRIPT_DIR/btrfs-backupctl" \
        "$SCRIPT_DIR/../bin/btrfs-backupctl"; do
        if [[ -x "$candidate" ]]; then
            realpath -m -- "$candidate"
            return 0
        fi
    done
    command -v btrfs-backupctl
}

write_status_record() {
    local target="$1"
    local state="$2"
    local phase="$3"
    local message="$4"
    local error="$5"
    local exit_code="$6"
    local finished_at="$7"
    local backupctl updated_at

    [[ -n "${RUN_ID:-}" ]] || return 0
    backupctl="$(backupctl_path)" || return 1
    updated_at="$(date --iso-8601=seconds)"

    "$backupctl" \
        --status-root "$STATUS_ROOT" \
        --history-root "$HISTORY_ROOT" \
        status write \
        "$target" \
        --profile-id "$PROFILE_ID" \
        --profile-name "$PROFILE_NAME" \
        --run-id "$RUN_ID" \
        --state "$state" \
        --phase "$phase" \
        --message "$message" \
        --current-source-name "$CURRENT_SOURCE_NAME" \
        --source-index "$SOURCE_INDEX" \
        --source-count "$SOURCE_COUNT" \
        --started-at "$RUN_STARTED_AT" \
        --updated-at "$updated_at" \
        --finished-at "$finished_at" \
        --error "$error" \
        --exit-code "$exit_code"
}

write_current_status() {
    local state="$1"
    local phase="$2"
    local message="$3"
    local error="${4:-}"
    local exit_code="${5:-0}"
    local finished_at="${6:-}"

    [[ -n "${RUN_ID:-}" && -n "$STATUS_ROOT" ]] || return 0
    write_status_record --current "$state" "$phase" "$message" "$error" "$exit_code" "$finished_at" \
        || bb_warn "Could not write current status JSON for profile $PROFILE_ID."
}

write_history_entry() {
    local state="$1"
    local phase="$2"
    local message="$3"
    local error="${4:-}"
    local exit_code="${5:-0}"
    local finished_at="$6"

    [[ -n "${RUN_ID:-}" && -n "$HISTORY_ROOT" ]] || return 0
    write_status_record --history "$state" "$phase" "$message" "$error" "$exit_code" "$finished_at" \
        || bb_warn "Could not write history JSON for profile $PROFILE_ID."
}

update_run_status() {
    local phase="$1"
    local message="$2"
    CURRENT_PHASE="$phase"
    CURRENT_MESSAGE="$message"
    write_current_status running "$phase" "$message"
}

cleanup_incoming_run() {
    local run_dir="$1"
    local entry

    [[ -n "$run_dir" && -d "$run_dir" ]] || return 0
    while IFS= read -r -d '' entry; do
        if bb_is_subvolume "$entry"; then
            btrfs subvolume delete -- "$entry" || true
        else
            rm -rf --one-file-system -- "$entry" || true
        fi
    done < <(find "$run_dir" -mindepth 1 -maxdepth 1 -print0 2>/dev/null)
    rmdir -- "$run_dir" 2>/dev/null || true
}

commit_received_snapshot() {
    local received_path="$1"
    local final_path="$2"
    local expected_received_uuid="$3"
    local committed_received_uuid

    btrfs subvolume snapshot -r "$received_path" "$final_path"
    committed_received_uuid="$(bb_subvolume_received_uuid "$final_path")"
    if [[ -z "$committed_received_uuid" \
        || "${committed_received_uuid,,}" != "${expected_received_uuid,,}" ]]; then
        btrfs subvolume delete -- "$final_path" 2>/dev/null || true
        bb_die "Committed snapshot Received UUID does not match the local snapshot UUID"
    fi
}

cleanup_stale_incoming() {
    local source_incoming_root="$1"
    local run_dir entry

    [[ -d "$source_incoming_root" ]] || return 0
    while IFS= read -r -d '' run_dir; do
        if bb_is_subvolume "$run_dir"; then
            btrfs subvolume delete -- "$run_dir"
            continue
        fi

        if [[ -d "$run_dir" ]]; then
            while IFS= read -r -d '' entry; do
                if bb_is_subvolume "$entry"; then
                    btrfs subvolume delete -- "$entry"
                else
                    rm -rf --one-file-system -- "$entry"
                fi
            done < <(find "$run_dir" -mindepth 1 -maxdepth 1 -print0)
            rmdir -- "$run_dir" 2>/dev/null || true
        else
            rm -f -- "$run_dir"
        fi
    done < <(find "$source_incoming_root" -mindepth 1 -maxdepth 1 -print0)
}

pending_marker_path() {
    local source_name="$1"
    printf '%s/pending-%s' "$PROFILE_STATE_DIR" "$source_name"
}

write_pending_marker() {
    local source_name="$1"
    local local_snapshot_path="$2"
    local marker backupctl

    marker="$(pending_marker_path "$source_name")"
    backupctl="$(backupctl_path)" || return 1
    "$backupctl" \
        state pending write \
        --profile-state-dir "$PROFILE_STATE_DIR" \
        --source-name "$source_name" \
        --local-snapshot-path "$local_snapshot_path" \
        --run-id "$RUN_ID" \
        --timestamp "$(date --iso-8601=seconds)"
    CURRENT_PENDING_MARKER="$marker"
}

clear_pending_marker() {
    local marker="${1:-$CURRENT_PENDING_MARKER}"
    local backupctl
    if [[ -n "$marker" ]]; then
        backupctl="$(backupctl_path)" || return 1
        "$backupctl" \
            state pending clear \
            --marker "$marker" \
            --profile-state-dir "$PROFILE_STATE_DIR" || true
    fi
    if [[ "$marker" == "$CURRENT_PENDING_MARKER" ]]; then
        CURRENT_PENDING_MARKER=""
    fi
}

remote_contains_received_uuid() {
    local remote_snapshot_dir="$1"
    local wanted_uuid="$2"
    local remote received_uuid

    while IFS= read -r remote; do
        received_uuid="$(bb_subvolume_received_uuid "$remote")"
        if [[ -n "$received_uuid" && "$received_uuid" != '-' \
            && "${received_uuid,,}" == "${wanted_uuid,,}" ]]; then
            return 0
        fi
    done < <(bb_list_direct_subvolumes "$remote_snapshot_dir")
    return 1
}

recover_pending_snapshot() {
    local source_name="$1"
    local local_snapshot_dir="$2"
    local remote_snapshot_dir="$3"
    local marker pending_path pending_uuid backupctl

    marker="$(pending_marker_path "$source_name")"
    [[ -r "$marker" ]] || return 0

    backupctl="$(backupctl_path)" || return 1
    pending_path="$("$backupctl" state pending read --marker "$marker" --field local_snapshot_path)"
    if [[ -z "$pending_path" ]] \
        || ! bb_path_is_within "$pending_path" "$local_snapshot_dir" \
        || [[ "$(basename -- "$pending_path")" != "$source_name-"* ]]; then
        bb_warn "Ignoring invalid pending marker for $source_name: $marker"
        clear_pending_marker "$marker"
        return 0
    fi

    if bb_is_subvolume "$pending_path"; then
        pending_uuid="$(bb_subvolume_uuid "$pending_path")"
        if [[ -n "$pending_uuid" ]] && remote_contains_received_uuid "$remote_snapshot_dir" "$pending_uuid"; then
            bb_log INFO "Recovered committed snapshot from an interrupted run: $pending_path"
        elif bb_bool_is_true "$KEEP_FAILED_LOCAL_SNAPSHOT"; then
            bb_warn "Keeping pending local snapshot by configuration: $pending_path"
        else
            bb_warn "Removing orphaned local snapshot from an interrupted run: $pending_path"
            btrfs subvolume delete -- "$pending_path"
        fi
    fi

    clear_pending_marker "$marker"
}

cleanup_current_transfer() {
    local target_available=0
    local remote_committed=0
    local local_uuid=""

    if mountpoint -q "$BACKUP_MOUNTPOINT"; then
        target_available=1
    fi

    if [[ -n "$CURRENT_INCOMING_RUN_DIR" ]] && (( target_available == 1 )); then
        bb_warn "Removing incomplete receive data: $CURRENT_INCOMING_RUN_DIR"
        cleanup_incoming_run "$CURRENT_INCOMING_RUN_DIR"
    fi

    if [[ -n "$CURRENT_LOCAL_SNAPSHOT" ]] && bb_is_subvolume "$CURRENT_LOCAL_SNAPSHOT"; then
        if (( target_available == 1 )) && [[ -n "$CURRENT_REMOTE_SNAPSHOT_DIR" ]]; then
            local_uuid="$(bb_subvolume_uuid "$CURRENT_LOCAL_SNAPSHOT")"
            if [[ -n "$local_uuid" ]] \
                && remote_contains_received_uuid "$CURRENT_REMOTE_SNAPSHOT_DIR" "$local_uuid"; then
                remote_committed=1
            fi
        fi

        if (( remote_committed == 1 )); then
            bb_warn "The remote snapshot was already committed; preserving its local incremental parent: $CURRENT_LOCAL_SNAPSHOT"
        elif (( target_available == 0 )); then
            bb_warn "The target is unavailable, so the local snapshot and pending marker are being preserved for recovery: $CURRENT_LOCAL_SNAPSHOT"
            return 0
        elif bb_bool_is_true "$KEEP_FAILED_LOCAL_SNAPSHOT"; then
            bb_warn "Keeping local snapshot from failed transfer by configuration: $CURRENT_LOCAL_SNAPSHOT"
        else
            bb_warn "Removing local snapshot from failed transfer: $CURRENT_LOCAL_SNAPSHOT"
            btrfs subvolume delete -- "$CURRENT_LOCAL_SNAPSHOT" || true
        fi
    fi

    clear_pending_marker
}

on_exit() {
    local status=$?
    local eject_status=0
    local final_state final_phase final_message final_error=""
    trap - EXIT INT TERM HUP
    set +e

    if (( status != 0 )); then
        cleanup_current_transfer
        final_state=failed
        final_phase="$CURRENT_PHASE"
        final_message="Backup failed during: $CURRENT_STEP"
        final_error="$CURRENT_STEP"
        bb_notify "$final_message"
    elif (( VALIDATE_ONLY == 1 )); then
        final_state=validated
        final_phase=validated
        final_message="Configuration and runtime preflight completed successfully."
        bb_notify "$final_message"
    elif (( RUN_SKIPPED == 1 )); then
        final_state=skipped
        final_phase=skipped
        final_message="A successful backup already exists for today; no new snapshot was created."
        bb_notify "$final_message"
    elif (( RUN_SUCCEEDED == 1 )); then
        final_state=succeeded
        final_phase=succeeded
        final_message="Backup completed successfully for $SOURCE_COUNT source(s)."
        bb_notify "$final_message"
    else
        final_state=exited
        final_phase="$CURRENT_PHASE"
        final_message="Backup exited."
    fi

    RUN_FINISHED_AT="$(date --iso-8601=seconds)"
    write_current_status "$final_state" "$final_phase" "$final_message" "$final_error" "$status" "$RUN_FINISHED_AT"
    write_history_entry "$final_state" "$final_phase" "$final_message" "$final_error" "$status" "$RUN_FINISHED_AT"

    if [[ -z "${INVOCATION_ID:-}" ]] \
        && (( NO_EJECT == 0 )) \
        && bb_bool_is_true "${AUTO_EJECT:-false}"; then
        bb_release_lock
        "$EJECT_SCRIPT_PATH" --from-runner --profile "$PROFILE_ID"
        eject_status=$?
        if (( eject_status != 0 && status == 0 )); then
            status=$eject_status
        fi
    else
        bb_release_lock
    fi

    exit "$status"
}

on_error() {
    local status=$?
    bb_log ERROR "Command failed during $CURRENT_STEP: ${BASH_COMMAND} (exit $status)" >&2
    return "$status"
}

on_interrupt() {
    CURRENT_STEP="interrupted by signal"
    exit "$1"
}

compute_config_fingerprint() {
    local backupctl
    backupctl="$(backupctl_path)" || return 1
    "$backupctl" \
        state fingerprint \
        --version "$BTRFS_BACKUP_VERSION" \
        --config "$PROFILE_JSON_FILE"
}

last_success_is_today() {
    local backupctl result

    backupctl="$(backupctl_path)" || return 1
    result="$("$backupctl" \
        state check-last-success \
        --profile-state-dir "$PROFILE_STATE_DIR" \
        --today "$(date +%F)" \
        --target-luks-uuid "$BACKUP_LUKS_UUID" \
        --config-fingerprint "$CONFIG_FINGERPRINT")" || return 1
    [[ "$result" == yes ]]
}

write_success_state() {
    local backupctl

    backupctl="$(backupctl_path)" || return 1
    "$backupctl" \
        state write-success \
        --profile-state-dir "$PROFILE_STATE_DIR" \
        --date "$(date +%F)" \
        --timestamp "$(date --iso-8601=seconds)" \
        --run-id "$RUN_ID" \
        --profile-id "$PROFILE_ID" \
        --profile-name "$PROFILE_NAME" \
        --source-count "$SOURCE_COUNT" \
        --target-luks-uuid "$BACKUP_LUKS_UUID" \
        --config-fingerprint "$CONFIG_FINGERPRINT"
}

ensure_target_mounted() {
    CURRENT_STEP="mounting backup target"
    update_run_status mounting-target "Mounting encrypted backup target."
    if ! mountpoint -q "$BACKUP_MOUNTPOINT"; then
        bb_notify "Mounting encrypted backup target."
        systemctl start "$BACKUP_MOUNT_UNIT"
    fi

    if ! mountpoint -q "$BACKUP_MOUNTPOINT"; then
        bb_die "Backup target is not mounted at $BACKUP_MOUNTPOINT"
    fi
}

validate_target() {
    CURRENT_STEP="validating backup target"
    update_run_status validating-target "Validating backup target."

    if [[ "$(bb_mount_fstype "$BACKUP_MOUNTPOINT")" != btrfs ]]; then
        bb_die "Backup target is not a Btrfs filesystem: $BACKUP_MOUNTPOINT"
    fi

    if ! bb_mount_uses_mapper "$BACKUP_MOUNTPOINT" "$BACKUP_MAPPER_NAME"; then
        bb_die "The filesystem mounted at $BACKUP_MOUNTPOINT is not /dev/mapper/$BACKUP_MAPPER_NAME"
    fi

    local mount_options
    mount_options="$(bb_mount_options "$BACKUP_MOUNTPOINT")"
    if [[ ",$mount_options," != *,rw,* ]]; then
        bb_die "Backup target is not mounted read-write: $BACKUP_MOUNTPOINT"
    fi

    local configured_device actual_device configured_real actual_real actual_luks_uuid
    configured_device="$BACKUP_DEVICE"
    actual_device="$(bb_mapper_underlying_device "$BACKUP_MAPPER_NAME")"
    configured_real="$(bb_canonical_device "$configured_device")"
    actual_real="$(bb_canonical_device "$actual_device")"

    if [[ -z "$configured_real" || -z "$actual_real" || "$configured_real" != "$actual_real" ]]; then
        bb_die "LUKS mapper $BACKUP_MAPPER_NAME does not use configured device $BACKUP_DEVICE (actual: ${actual_device:-unknown})."
    fi

    actual_luks_uuid="$(cryptsetup luksUUID "$configured_device" 2>/dev/null || true)"
    if [[ -z "$actual_luks_uuid" || "${actual_luks_uuid,,}" != "${BACKUP_LUKS_UUID,,}" ]]; then
        bb_die "LUKS UUID mismatch for $BACKUP_DEVICE"
    fi

    if [[ -n "$BACKUP_BTRFS_UUID" ]]; then
        local actual_btrfs_uuid
        actual_btrfs_uuid="$(findmnt -n -o UUID -M "$BACKUP_MOUNTPOINT" 2>/dev/null || true)"
        if [[ -z "$actual_btrfs_uuid" || "${actual_btrfs_uuid,,}" != "${BACKUP_BTRFS_UUID,,}" ]]; then
            bb_die "Btrfs UUID mismatch at $BACKUP_MOUNTPOINT"
        fi
    fi

    if ! bb_path_is_within "$REMOTE_ROOT" "$BACKUP_MOUNTPOINT"; then
        bb_die "REMOTE_ROOT escapes the backup mountpoint: $REMOTE_ROOT"
    fi
    if ! bb_path_is_within "$INCOMING_ROOT" "$BACKUP_MOUNTPOINT"; then
        bb_die "INCOMING_ROOT escapes the backup mountpoint: $INCOMING_ROOT"
    fi

    install -d -m0700 "$REMOTE_ROOT" "$INCOMING_ROOT"
    bb_check_minimum_free_space "$BACKUP_MOUNTPOINT" "$MIN_TARGET_FREE_BYTES" "backup target"
}

generate_snapshot_path() {
    local source_name="$1"
    local local_snapshot_dir="$2"
    local timestamp base candidate sequence

    timestamp="$(date -u +%Y-%m-%dT%H%M%SZ)"
    base="${source_name}-${timestamp}"
    candidate="$local_snapshot_dir/$base"
    sequence=0

    while [[ -e "$candidate" ]]; do
        ((sequence += 1))
        candidate="${local_snapshot_dir}/${base}-$(printf '%02d' "$sequence")"
    done

    printf '%s\n' "$candidate"
}

find_incremental_parent() {
    local local_snapshot_dir="$1"
    local remote_snapshot_dir="$2"
    local current_snapshot="$3"
    local source_name="$4"
    local remote local_snapshot received_uuid local_uuid base
    declare -A remote_by_received_uuid=()

    while IFS= read -r remote; do
        base="$(basename -- "$remote")"
        [[ "$base" == "$source_name-"* ]] || continue
        bb_is_readonly_subvolume "$remote" || continue
        received_uuid="$(bb_subvolume_received_uuid "$remote")"
        [[ -n "$received_uuid" && "$received_uuid" != '-' ]] || continue
        remote_by_received_uuid["${received_uuid,,}"]="$remote"
    done < <(bb_list_direct_subvolumes "$remote_snapshot_dir")

    while IFS= read -r local_snapshot; do
        [[ "$local_snapshot" != "$current_snapshot" ]] || continue
        base="$(basename -- "$local_snapshot")"
        [[ "$base" == "$source_name-"* ]] || continue
        bb_is_readonly_subvolume "$local_snapshot" || continue
        local_uuid="$(bb_subvolume_uuid "$local_snapshot")"
        [[ -n "$local_uuid" ]] || continue
        if [[ -n "${remote_by_received_uuid[${local_uuid,,}]+x}" ]]; then
            printf '%s\n' "$local_snapshot"
            return 0
        fi
    done < <(bb_list_direct_subvolumes "$local_snapshot_dir" | sort -r)

    return 1
}

snapshot_directory_has_subvolumes() {
    local directory="$1"
    local source_name="$2"
    local path base

    while IFS= read -r path; do
        base="$(basename -- "$path")"
        [[ "$base" == "$source_name-"* ]] && return 0
    done < <(bb_list_direct_subvolumes "$directory")
    return 1
}

receive_snapshot() {
    local snapshot_path="$1"
    local parent_path="$2"
    local receive_directory="$3"

    if [[ -n "$parent_path" ]]; then
        bb_log INFO "Sending incremental stream with parent: $parent_path"
        if command -v pv >/dev/null 2>&1; then
            btrfs send -p "$parent_path" "$snapshot_path" \
                | pv -ptebar \
                | btrfs receive "$receive_directory"
        else
            btrfs send -p "$parent_path" "$snapshot_path" \
                | btrfs receive "$receive_directory"
        fi
    else
        bb_log INFO "Sending full stream."
        if command -v pv >/dev/null 2>&1; then
            btrfs send "$snapshot_path" \
                | pv -ptebar \
                | btrfs receive "$receive_directory"
        else
            btrfs send "$snapshot_path" \
                | btrfs receive "$receive_directory"
        fi
    fi
}

prune_snapshots() {
    local directory="$1"
    local source_name="$2"
    local keep_count="$3"
    local path base
    local snapshots=()

    bb_validate_uint retention "$keep_count"
    (( keep_count == 0 )) && return 0

    while IFS= read -r path; do
        base="$(basename -- "$path")"
        [[ "$base" == "$source_name-"* ]] || continue
        snapshots+=("$path")
    done < <(bb_list_direct_subvolumes "$directory" | sort)

    local total="${#snapshots[@]}"
    (( total > keep_count )) || return 0

    local delete_count=$((total - keep_count))
    local index
    for ((index = 0; index < delete_count; index++)); do
        bb_log INFO "Pruning snapshot: ${snapshots[$index]}"
        btrfs subvolume delete -- "${snapshots[$index]}"
    done
}

process_source_values() {
    local source_label="$1"
    shift
    local values=("$@")
    local source_name="${values[0]}"
    local source_subvolume="${values[1]}"
    local local_snapshot_dir="${values[2]}"
    local remote_subdir="${values[3]}"
    local remote_retention="${values[4]}"
    local local_retention="${values[5]}"
    local remote_snapshot_dir="$REMOTE_ROOT/$remote_subdir"
    local source_incoming_root="$INCOMING_ROOT/$source_name"

    if [[ ! -d "$source_subvolume" ]] || ! bb_is_subvolume "$source_subvolume"; then
        bb_die "SOURCE_SUBVOLUME is not an available Btrfs subvolume: $source_subvolume"
    fi

    install -d -m0700 "$local_snapshot_dir"
    if ! bb_paths_are_same_filesystem "$source_subvolume" "$local_snapshot_dir"; then
        bb_die "LOCAL_SNAPSHOT_DIR must be on the same Btrfs filesystem as $source_subvolume"
    fi

    if bb_paths_are_same_filesystem "$source_subvolume" "$BACKUP_MOUNTPOINT"; then
        bb_die "SOURCE_SUBVOLUME must not be on the backup target filesystem: $source_subvolume"
    fi

    if bb_path_is_within "$local_snapshot_dir" "$BACKUP_MOUNTPOINT"; then
        bb_die "LOCAL_SNAPSHOT_DIR must not be inside the backup target: $local_snapshot_dir"
    fi

    bb_check_minimum_free_space "$local_snapshot_dir" "$MIN_LOCAL_FREE_BYTES" "local snapshots for $source_name"

    if [[ -n "${SEEN_SOURCE_NAMES[$source_name]+x}" ]]; then
        bb_die "Duplicate SOURCE_NAME=$source_name in $source_label and ${SEEN_SOURCE_NAMES[$source_name]}"
    fi
    SEEN_SOURCE_NAMES["$source_name"]="$source_label"
    local snapshot_path snapshot_name incoming_run_dir received_path parent_path=""
    local local_uuid received_uuid

    CURRENT_STEP="preparing source $source_name"
    CURRENT_REMOTE_SNAPSHOT_DIR="$remote_snapshot_dir"

    if ! bb_path_is_within "$remote_snapshot_dir" "$REMOTE_ROOT"; then
        bb_die "Remote source directory escapes REMOTE_ROOT, possibly through a symlink: $remote_snapshot_dir"
    fi
    if ! bb_path_is_within "$source_incoming_root" "$INCOMING_ROOT"; then
        bb_die "Incoming source directory escapes INCOMING_ROOT, possibly through a symlink: $source_incoming_root"
    fi

    install -d -m0700 "$remote_snapshot_dir" "$source_incoming_root"

    if (( VALIDATE_ONLY == 1 )); then
        CURRENT_SOURCE_NAME="$source_name"
        SOURCE_INDEX=$((SOURCE_COUNT + 1))
        update_run_status validating-source "Validated source $source_name."
        if [[ -n "$(find "$source_incoming_root" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
            bb_warn "Stale incoming data exists for $source_name and will be cleaned during the next real backup."
        fi
        bb_log INFO "Validated source $source_name: $source_subvolume"
        ((SOURCE_COUNT += 1))
        return 0
    fi

    cleanup_stale_incoming "$source_incoming_root"
    recover_pending_snapshot "$source_name" "$local_snapshot_dir" "$remote_snapshot_dir"

    CURRENT_STEP="creating readonly snapshot for $source_name"
    CURRENT_SOURCE_NAME="$source_name"
    SOURCE_INDEX=$((SOURCE_COUNT + 1))
    update_run_status creating-snapshot "Creating readonly snapshot for $source_name."
    bb_notify "Creating readonly snapshot for $source_name."
    snapshot_path="$(generate_snapshot_path "$source_name" "$local_snapshot_dir")"
    snapshot_name="$(basename -- "$snapshot_path")"
    CURRENT_LOCAL_SNAPSHOT="$snapshot_path"
    write_pending_marker "$source_name" "$snapshot_path"
    btrfs subvolume snapshot -r "$source_subvolume" "$snapshot_path"
    if ! bb_is_readonly_subvolume "$snapshot_path"; then
        bb_die "New local snapshot is not readonly: $snapshot_path"
    fi

    CURRENT_STEP="selecting incremental parent for $source_name"
    update_run_status selecting-parent "Selecting incremental parent for $source_name."
    if parent_path="$(find_incremental_parent "$local_snapshot_dir" "$remote_snapshot_dir" "$snapshot_path" "$source_name")"; then
        bb_log INFO "Verified incremental parent by UUID: $parent_path"
    elif snapshot_directory_has_subvolumes "$remote_snapshot_dir" "$source_name" && bb_bool_is_true "$INCREMENTAL_REQUIRED"; then
        bb_die "Remote snapshots exist for $source_name, but no UUID-matching local parent was found."
    else
        parent_path=""
    fi

    CURRENT_STEP="receiving snapshot for $source_name"
    update_run_status transferring "Transferring snapshot for $source_name."
    incoming_run_dir="$source_incoming_root/$RUN_ID"
    install -d -m0700 "$incoming_run_dir"
    CURRENT_INCOMING_RUN_DIR="$incoming_run_dir"
    bb_notify "Transferring snapshot for $source_name."
    receive_snapshot "$snapshot_path" "$parent_path" "$incoming_run_dir"

    received_path="$incoming_run_dir/$snapshot_name"
    CURRENT_RECEIVED_SUBVOLUME="$received_path"
    if ! bb_is_subvolume "$received_path"; then
        bb_die "btrfs receive did not create the expected subvolume: $received_path"
    fi
    if ! bb_is_readonly_subvolume "$received_path"; then
        bb_die "Received subvolume is not readonly: $received_path"
    fi

    local_uuid="$(bb_subvolume_uuid "$snapshot_path")"
    received_uuid="$(bb_subvolume_received_uuid "$received_path")"
    if [[ -z "$local_uuid" || -z "$received_uuid" || "${local_uuid,,}" != "${received_uuid,,}" ]]; then
        bb_die "Received UUID does not match the local snapshot UUID for $source_name"
    fi

    local final_path="$remote_snapshot_dir/$snapshot_name"
    if [[ -e "$final_path" ]]; then
        bb_die "Destination snapshot already exists: $final_path"
    fi

    CURRENT_STEP="committing received snapshot for $source_name"
    update_run_status committing "Committing received snapshot for $source_name."
    commit_received_snapshot "$received_path" "$final_path" "$local_uuid"
    btrfs subvolume delete -- "$received_path"
    sync -f "$remote_snapshot_dir" 2>/dev/null || sync
    rmdir -- "$incoming_run_dir" 2>/dev/null || true
    clear_pending_marker

    CURRENT_LOCAL_SNAPSHOT=""
    CURRENT_INCOMING_RUN_DIR=""
    CURRENT_RECEIVED_SUBVOLUME=""
    CURRENT_REMOTE_SNAPSHOT_DIR=""

    CURRENT_STEP="pruning snapshots for $source_name"
    update_run_status pruning "Pruning snapshots for $source_name."
    prune_snapshots "$remote_snapshot_dir" "$source_name" "$remote_retention"
    prune_snapshots "$local_snapshot_dir" "$source_name" "$local_retention"
    sync -f "$remote_snapshot_dir" 2>/dev/null || sync

    ((SOURCE_COUNT += 1))
    bb_notify "Snapshot for $source_name was transferred and verified."
}

process_profile_json_sources() {
    local backupctl parser_output
    local source_records=()
    local index=0
    local values=()

    backupctl="$(backupctl_path)" || return 1
    parser_output="$("$backupctl" profile sources --file "$PROFILE_JSON_FILE")" || return $?
    mapfile -t source_records <<< "$parser_output"
    if (( ${#source_records[@]} == 0 )); then
        bb_die "No enabled source definitions found in $PROFILE_JSON_FILE"
    fi
    if (( ${#source_records[@]} % 6 != 0 )); then
        bb_die "Invalid source record output from btrfs-backupctl."
    fi

    while (( index < ${#source_records[@]} )); do
        values=("${source_records[@]:index:6}")
        process_source_values "$PROFILE_JSON_FILE:${values[0]}" "${values[@]}"
        index=$((index + 6))
    done
}

bb_require_root
bb_require_commands \
    basename btrfs cat chmod cryptsetup date df dirname find findmnt flock grep head id install \
    mountpoint mv readlink realpath rm rmdir sed sort stat sync systemctl \
    systemd-escape tail tr
load_main_config

if ! bb_acquire_lock "$LOCK_FILE"; then
    bb_log INFO "Another backup or eject operation is already running; exiting without changes."
    exit 0
fi

trap on_exit EXIT
trap on_error ERR
trap 'on_interrupt 130' INT
trap 'on_interrupt 143' TERM
trap 'on_interrupt 129' HUP

RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)-$$-${RANDOM}"
RUN_STARTED_AT="$(date --iso-8601=seconds)"
write_current_status starting starting "Backup run started."

declare -A SEEN_SOURCE_NAMES=()
[[ -n "$PROFILE_JSON_FILE" ]] || bb_die "Profile JSON is required for runtime source definitions."
bb_assert_trusted_config_file "$PROFILE_JSON_FILE"
CONFIG_FINGERPRINT="$(compute_config_fingerprint)"

if (( VALIDATE_ONLY == 0 && FORCE_RUN == 0 )) \
    && bb_bool_is_true "$DAILY_LIMIT" \
    && last_success_is_today; then
    RUN_SKIPPED=1
    exit 0
fi

ensure_target_mounted
validate_target

process_profile_json_sources

if (( SOURCE_COUNT == 0 )); then
    bb_die "No enabled source definitions were found."
fi

if (( VALIDATE_ONLY == 1 )); then
    exit 0
fi

CURRENT_STEP="synchronizing backup target"
sync -f "$BACKUP_MOUNTPOINT" 2>/dev/null || sync
CURRENT_STEP="recording successful backup state"
write_success_state
RUN_SUCCEEDED=1
