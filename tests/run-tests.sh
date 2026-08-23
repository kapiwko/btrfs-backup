#!/usr/bin/env bash
set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C

MODE=full
case "${1:-}" in
    "") ;;
    --full) MODE=full ;;
    --static-only) MODE=static ;;
    -h|--help)
        cat <<'USAGE'
Usage: tests/run-tests.sh [--full|--static-only]

  --full         Run all mocked runtime tests (default).
  --static-only  Run syntax and rendering validation only.
USAGE
        exit 0
        ;;
    *)
        printf 'Unknown option: %s\n' "$1" >&2
        exit 2
        ;;
esac

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-tests.XXXXXX)"
MAPPER_NAME="bbtestbackup$$"
MAPPER_ROOT="$TEST_ROOT/dev/mapper"
MAPPER_PATH="$MAPPER_ROOT/$MAPPER_NAME"
TESTS_RUN=0

cleanup() {
    rm -f -- "$MAPPER_PATH" 2>/dev/null || true
    rm -rf -- "$TEST_ROOT"
}
trap cleanup EXIT

make_invoker_owned() {
    local path="$1"
    if [[ -n "${SUDO_UID:-}" && "$SUDO_UID" =~ ^[0-9]+$ ]]; then
        chown "$SUDO_UID:${SUDO_GID:-$SUDO_UID}" "$path"
    fi
}

pass() {
    TESTS_RUN=$((TESTS_RUN + 1))
    printf 'ok %d - %s\n' "$TESTS_RUN" "$1"
}

fail() {
    printf 'not ok - %s\n' "$1" >&2
    exit 1
}

assert_file() {
    [[ -f "$1" ]] || fail "expected file: $1"
}

assert_dir() {
    [[ -d "$1" ]] || fail "expected directory: $1"
}

assert_not_exists() {
    [[ ! -e "$1" ]] || fail "path should not exist: $1"
}

assert_contains() {
    local file="$1"
    local pattern="$2"
    grep -Fq -- "$pattern" "$file" || fail "missing '$pattern' in $file"
}

assert_not_contains() {
    local file="$1"
    local pattern="$2"
    if grep -Fq -- "$pattern" "$file"; then
        fail "unexpected '$pattern' in $file"
    fi
}

count_subvolumes() {
    local directory="$1"
    find "$directory" -mindepth 1 -maxdepth 1 -type d -name '*.snapshot' -o -type d -name 'root-*' -o -type d -name 'home-*' 2>/dev/null | wc -l
}

write_meta() {
    local path="$1"
    local uuid="$2"
    local received_uuid="${3:--}"
    mkdir -p -- "$path"
    cat > "$path/.mock-subvolume" <<META
UUID=$uuid
RECEIVED_UUID=$received_uuid
RO=true
META
}

syntax_test() {
    make -C "$ROOT" >/dev/null
    ctest --test-dir "$ROOT/build" --output-on-failure >/dev/null
    mapfile -t scripts < <(find "$ROOT" -type f \( -name '*.sh' -o -name '*.install' -o \( -path "$ROOT/bin/*" ! -path "$ROOT/bin/__pycache__/*" \) \) | sort)
    local script
    for script in "${scripts[@]}"; do
        bash -n "$script"
    done
    pass 'all Bash files parse'
}

render_test() {
    local output="$TEST_ROOT/rendered"
    local profile="$output/config/profile.json"

    install -d -m0750 "$output/config" "$output/systemd" "$output/udev"
    "$ROOT/bin/btrfs-backupctl" profile create \
        --output "$profile" \
        --profile laptop \
        --name 'Laptop backup' \
        --device /dev/disk/by-uuid/11111111-2222-3333-4444-555555555555 \
        --luks-uuid 11111111-2222-3333-4444-555555555555 \
        --btrfs-uuid 66666666-7777-8888-9999-aaaaaaaaaaaa \
        --partition-uuid aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee \
        --mapper-name backupdisk \
        --mount-point /mnt/backup \
        --remote-retention 30 \
        --local-retention 30 \
        --minimum-target-free-bytes 5368709120 \
        --minimum-local-free-bytes 1073741824 \
        --notify-user tester \
        --source root root / /.snapshots/btrfs-backup/root root 30 30 \
        --source home home /home /.snapshots/btrfs-backup/home home 45 20 >/dev/null
    "$ROOT/bin/btrfs-backupctl" \
        profile \
        --etc-root "$output/config" \
        --udev-root "$output/udev" \
        --public-root "$output/public/profiles" \
        save --file "$profile" >/dev/null
    "$ROOT/bin/btrfs-backupctl" installation render \
        --file "$profile" \
        --output-dir "$output" \
        --backup-command "$ROOT/scripts/btrfs-backup.sh" \
        --eject-script "$ROOT/scripts/btrfs-backup-eject.sh" \
        --keyfile /root/keys/backupdisk.key
    "$ROOT/bin/btrfs-backupctl" installation validate --rendered-root "$output" >/dev/null

    assert_file "$output/config/profile.json"
    assert_file "$output/config/profiles/laptop/profile.json"
    assert_contains "$output/config/profile.json" '"profileId": "laptop"'
    assert_file "$output/systemd/btrfs-backup.service"
    assert_file "$output/systemd/btrfs-backup@.service"
    assert_file "$output/udev/99-btrfs-backup-laptop.rules"
    assert_not_exists "$output/udev/99-btrfs-backup.rules"
    assert_not_contains "$output/udev/99-btrfs-backup-laptop.rules" 'ACTION=="remove"'
    assert_contains "$output/udev/99-btrfs-backup-laptop.rules" 'btrfs-backup@laptop.service'
    assert_not_contains "$output/systemd/btrfs-backup.service" 'WantedBy='
    assert_not_contains "$output/systemd/btrfs-backup.service" 'Requires=mnt-backup.mount'
    assert_contains "$output/systemd/btrfs-backup.service" 'ExecStart='
    assert_contains "$output/systemd/btrfs-backup.service" '--profile laptop'
    assert_contains "$output/config/fstab.fragment" 'noauto'
    assert_contains "$output/config/fstab.fragment" 'x-systemd.requires=systemd-cryptsetup@backupdisk.service'
    if grep -R -q '{{' "$output"; then
        fail 'rendered output contains unresolved placeholders'
    fi
    pass 'backupctl renders validated multi-source configuration'
}

profile_json_test() {
    local rendered="$TEST_ROOT/profile-json-rendered"
    local saved="$TEST_ROOT/profile-json-saved"

    "$ROOT/bin/btrfs-backupctl" profile validate --file "$ROOT/config/profile.example.json" >/dev/null
    "$ROOT/bin/btrfs-backupctl" profile render \
        --file "$ROOT/config/profile.example.json" \
        --output-dir "$rendered" >/dev/null

    assert_not_exists "$rendered/etc/btrfs-backup/profiles.d/default.env"
    assert_file "$rendered/etc/btrfs-backup/profiles/default/profile.json"
    assert_file "$rendered/etc/udev/rules.d/99-btrfs-backup-default.rules"
    assert_file "$rendered/var/lib/btrfs-backup/public/profiles/default.json"
    assert_contains "$rendered/etc/btrfs-backup/profiles/default/profile.json" '"id": "home"'
    assert_contains "$rendered/etc/udev/rules.d/99-btrfs-backup-default.rules" 'btrfs-backup@default.service'

    "$ROOT/bin/btrfs-backupctl" profile \
        --etc-root "$saved/etc/btrfs-backup" \
        --udev-root "$saved/etc/udev/rules.d" \
        --public-root "$saved/var/lib/btrfs-backup/public/profiles" \
        save --file "$ROOT/config/profile.example.json" >/dev/null

    assert_not_exists "$saved/etc/btrfs-backup/profiles.d/default.env"
    assert_file "$saved/etc/btrfs-backup/profiles/default/profile.json"
    assert_file "$saved/etc/udev/rules.d/99-btrfs-backup-default.rules"
    assert_file "$saved/var/lib/btrfs-backup/public/profiles/default.json"
    "$ROOT/bin/btrfs-backupctl" profile \
        --etc-root "$saved/etc/btrfs-backup" \
        show --profile default > "$saved/show.json"
    assert_contains "$saved/show.json" '"profileId": "default"'
    "$ROOT/bin/btrfs-backupctl" profile \
        --etc-root "$saved/etc/btrfs-backup" \
        export --profile default --output "$saved/exported.json" >/dev/null
    assert_file "$saved/exported.json"
    assert_contains "$saved/exported.json" '"profileId": "default"'
    pass 'profile JSON validates, renders, and saves generated runtime files'
}

status_writer_cli_test() {
    local status_root="$TEST_ROOT/status-writer-cli/status"
    local history_root="$TEST_ROOT/status-writer-cli/history"
    local run_id="20260823T082504Z-123-456"
    local current="$status_root/default/current.json"
    local history="$history_root/default/$run_id.json"
    local last="$history_root/default/last.json"
    local message=$'Backup "done"\nLine'

    "$ROOT/bin/btrfs-backupctl" \
        --status-root "$status_root" \
        --history-root "$history_root" \
        status write \
        --current \
        --history \
        --profile-id default \
        --profile-name 'Default backup' \
        --run-id "$run_id" \
        --state succeeded \
        --phase complete \
        --message "$message" \
        --current-source-name home \
        --source-index 2 \
        --source-count 2 \
        --started-at '2026-08-23T08:24:00+02:00' \
        --updated-at '2026-08-23T08:25:04+02:00' \
        --finished-at '2026-08-23T08:25:04+02:00' \
        --error-code '' \
        --error-message '' \
        --recoverable false \
        --suggested-action '' \
        --can-cancel false \
        --safe-to-remove false \
        --exit-code 0

    assert_file "$current"
    assert_file "$history"
    assert_file "$last"
    cmp -s "$history" "$last" \
        || fail 'last history status does not match run history status'
    assert_contains "$current" '"schemaVersion": 1'
    assert_contains "$current" '"state": "succeeded"'
    assert_contains "$current" '"message": "Backup \"done\"\nLine"'
    assert_contains "$current" '"errorCode": ""'
    assert_contains "$current" '"errorMessage": ""'
    assert_contains "$current" '"canCancel": false'
    assert_contains "$current" '"safeToRemove": false'
    assert_contains "$history" '"currentSourceName": "home"'

    "$ROOT/bin/btrfs-backupctl" \
        --status-root "$status_root" \
        --history-root "$history_root" \
        status show --profile default --human \
        | grep -q 'Default backup: succeeded' \
        || fail 'btrfs-backupctl did not render written status'

    "$ROOT/bin/btrfs-backupctl" \
        --history-root "$history_root" \
        status history --profile default --limit 1 \
        | grep -q '"runId": "20260823T082504Z-123-456"' \
        || fail 'btrfs-backupctl did not render written history'

    pass 'backupctl writes runtime status and history through the CLI'
}

create_mock_commands() {
    local mockbin="$1"
    mkdir -p "$mockbin"

    cat > "$mockbin/btrfs" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
cmd="${1:-}"
shift || true
meta_get() {
    local path="$1" key="$2"
    sed -n "s/^${key}=//p" "$path/.mock-subvolume" | head -n1
}
case "$cmd" in
    subvolume)
        sub="${1:-}"; shift || true
        case "$sub" in
            show)
                path="$1"
                [[ -f "$path/.mock-subvolume" ]] || exit 1
                uuid="$(meta_get "$path" UUID)"
                received="$(meta_get "$path" RECEIVED_UUID)"
                printf 'Name: %s\n' "$(basename -- "$path")"
                printf 'UUID: %s\n' "$uuid"
                printf 'Parent UUID: -\n'
                printf 'Received UUID: %s\n' "$received"
                printf 'Flags: readonly\n'
                ;;
            snapshot)
                [[ "${1:-}" == -r ]] && shift
                source_path="$1"; destination="$2"
                [[ -f "$source_path/.mock-subvolume" ]] || exit 1
                received="$(meta_get "$source_path" RECEIVED_UUID)"
                mkdir -p -- "$destination"
                uuid="uuid-$(basename -- "$destination")"
                cat > "$destination/.mock-subvolume" <<META
UUID=$uuid
RECEIVED_UUID=$received
RO=true
META
                printf 'SNAPSHOT %s %s\n' "$source_path" "$destination" >> "$MOCK_LOG"
                ;;
            delete)
                [[ "${1:-}" == -- ]] && shift
                path="$1"
                printf 'DELETE %s\n' "$path" >> "$MOCK_LOG"
                rm -rf -- "$path"
                ;;
            *) exit 2 ;;
        esac
        ;;
    property)
        [[ "${1:-}" == get ]] || exit 2
        path="${@: -2:1}"
        [[ -f "$path/.mock-subvolume" ]] || exit 1
        printf 'ro=true\n'
        ;;
    send)
        if [[ "${1:-}" == -p ]]; then
            parent="$2"
            snapshot="$3"
            printf 'SEND_INCREMENTAL %s %s\n' "$parent" "$snapshot" >> "$MOCK_LOG"
        else
            snapshot="$1"
            printf 'SEND_FULL %s\n' "$snapshot" >> "$MOCK_LOG"
        fi
        printf '%s\n' "$snapshot"
        ;;
    receive)
        destination="$1"
        IFS= read -r snapshot
        name="$(basename -- "$snapshot")"
        uuid="$(meta_get "$snapshot" UUID)"
        received_path="$destination/$name"
        mkdir -p -- "$received_path"
        cat > "$received_path/.mock-subvolume" <<META
UUID=remote-$uuid
RECEIVED_UUID=$uuid
RO=true
META
        printf 'RECEIVE %s %s\n' "$snapshot" "$received_path" >> "$MOCK_LOG"
        if [[ "${MOCK_RECEIVE_UNMOUNT:-0}" == 1 ]]; then
            rm -f -- "$MOCK_MOUNTPOINT/.mock-mounted"
        fi
        if [[ "${MOCK_RECEIVE_FAIL:-0}" == 1 ]]; then
            exit 9
        fi
        ;;
    *) exit 2 ;;
esac
MOCK

    cat > "$mockbin/mountpoint" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ "${1:-}" == -q ]] && shift
[[ -f "$1/.mock-mounted" ]]
MOCK

    cat > "$mockbin/systemctl" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
case "${1:-}" in
    start)
        mkdir -p -- "$MOCK_MOUNTPOINT" "$MOCK_MAPPER_ROOT"
        touch "$MOCK_MOUNTPOINT/.mock-mounted"
        ln -sfn "$MOCK_DM_DEVICE" "$MOCK_MAPPER_ROOT/$MOCK_MAPPER_NAME"
        printf 'SYSTEMCTL_START %s\n' "${2:-}" >> "$MOCK_LOG"
        ;;
    *) exit 0 ;;
esac
MOCK

    cat > "$mockbin/findmnt" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
output=""
mode=""
path=""
args=("$@")
for ((i=0; i<${#args[@]}; i++)); do
    case "${args[$i]}" in
        -o) output="${args[$((i+1))]}" ;;
        -M) mode=M; path="${args[$((i+1))]}" ;;
        -T) mode=T; path="${args[$((i+1))]}" ;;
    esac
done
if [[ "$output" == SOURCE,TARGET ]]; then
    if [[ -f "$MOCK_MOUNTPOINT/.mock-mounted" ]]; then
        printf '%s/%s %s\n' "$MOCK_MAPPER_ROOT" "$MOCK_MAPPER_NAME" "$MOCK_MOUNTPOINT"
    fi
    exit 0
fi
if [[ "$mode" == M ]]; then
    case "$output" in
        SOURCE) printf '%s/%s\n' "$MOCK_MAPPER_ROOT" "$MOCK_MAPPER_NAME" ;;
        FSTYPE) printf 'btrfs\n' ;;
        OPTIONS) printf 'rw,noatime,compress=zstd\n' ;;
        UUID) printf '%s\n' "$MOCK_TARGET_UUID" ;;
        MAJ:MIN) printf '253:9\n' ;;
        *) exit 1 ;;
    esac
    exit 0
fi
if [[ "$mode" == T ]]; then
    if [[ "$path" == "$MOCK_MOUNTPOINT" || "$path" == "$MOCK_MOUNTPOINT/"* ]]; then
        case "$output" in
            UUID) printf '%s\n' "$MOCK_TARGET_UUID" ;;
            MAJ:MIN) printf '253:9\n' ;;
            *) exit 1 ;;
        esac
    else
        case "$output" in
            UUID) printf '%s\n' "$MOCK_SOURCE_UUID" ;;
            MAJ:MIN) printf '259:2\n' ;;
            *) exit 1 ;;
        esac
    fi
    exit 0
fi
exit 1
MOCK

    cat > "$mockbin/readlink" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == -f ]]; then
    shift
fi
if [[ "${1:-}" == -- ]]; then
    shift
fi
case "${1:-}" in
    "/dev/disk/by-uuid/$MOCK_LUKS_UUID")
        printf '%s\n' "$MOCK_PHYSICAL_DEVICE"
        ;;
    "$MOCK_MAPPER_ROOT/$MOCK_MAPPER_NAME")
        printf '%s\n' "$MOCK_DM_DEVICE"
        ;;
    *)
        /usr/bin/readlink -f -- "$1"
        ;;
esac
MOCK

    cat > "$mockbin/cryptsetup" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
case "${1:-}" in
    status)
        cat <<STATUS
/dev/mapper/${2:-$MOCK_MAPPER_NAME} is active.
  type:    LUKS2
  device:  $MOCK_PHYSICAL_DEVICE
STATUS
        ;;
    luksUUID)
        printf '%s\n' "$MOCK_LUKS_UUID"
        ;;
    close)
        rm -f -- "$MOCK_MAPPER_ROOT/${2:-$MOCK_MAPPER_NAME}"
        printf 'CRYPT_CLOSE %s\n' "${2:-$MOCK_MAPPER_NAME}" >> "$MOCK_LOG"
        ;;
    *) exit 2 ;;
esac
MOCK

    cat > "$mockbin/umount" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ "${1:-}" == -- ]] && shift
rm -f -- "$1/.mock-mounted"
printf 'UMOUNT %s\n' "$1" >> "$MOCK_LOG"
MOCK

    cat > "$mockbin/blockdev" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
printf 'BLOCKDEV %s\n' "$*" >> "$MOCK_LOG"
MOCK

    cat > "$mockbin/udevadm" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK

    cat > "$mockbin/sync" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK

    chmod 0755 "$mockbin"/*
}

prepare_runtime_fixture() {
    RUNTIME="$TEST_ROOT/runtime"
    MOCKBIN="$RUNTIME/mockbin"
    MOCK_LOG="$RUNTIME/mock.log"
    MOCK_MOUNTPOINT="$RUNTIME/mnt/backup"
    MOCK_LUKS_UUID=11111111-2222-3333-4444-555555555555
    MOCK_PHYSICAL_DEVICE="$RUNTIME/dev/sdb1"
    MOCK_DM_DEVICE="$RUNTIME/dev/dm-0"
    MOCK_MAPPER_ROOT="$MAPPER_ROOT"
    MOCK_DEVICE_LINK="/dev/disk/by-uuid/$MOCK_LUKS_UUID"
    MOCK_TARGET_UUID=aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
    MOCK_SOURCE_UUID=99999999-8888-7777-6666-555555555555
    SOURCES_DIR="$RUNTIME/config/sources.d"
    STATE_DIR="$RUNTIME/state"
    PROFILE_STATE_DIR="$STATE_DIR/profiles/default"
    STATUS_ROOT="$RUNTIME/run/status"
    HISTORY_ROOT="$RUNTIME/history"
    LOCK_FILE="$RUNTIME/run/backup.lock"
    SOURCE_ROOT="$RUNTIME/sources/root"
    SOURCE_HOME="$RUNTIME/sources/home"
    LOCAL_ROOT="$RUNTIME/local/root"
    LOCAL_HOME="$RUNTIME/local/home"
    CONFIG_FILE="$RUNTIME/config/legacy.env"
    PROFILE_JSON_FILE="$RUNTIME/config/profiles/default/profile.json"

    rm -rf -- "$RUNTIME"
    mkdir -p "$MOCKBIN" "$RUNTIME/dev" "$SOURCES_DIR" "$(dirname -- "$CONFIG_FILE")" "$(dirname -- "$PROFILE_JSON_FILE")" \
        "$STATE_DIR" "$SOURCE_ROOT" "$SOURCE_HOME" "$LOCAL_ROOT" "$LOCAL_HOME" "$MOCK_MOUNTPOINT" "$MOCK_MAPPER_ROOT"
    : > "$MOCK_PHYSICAL_DEVICE"
    : > "$MOCK_DM_DEVICE"
    ln -sfn "$MOCK_DM_DEVICE" "$MAPPER_PATH"
    : > "$MOCK_LOG"

    write_meta "$SOURCE_ROOT" source-root
    write_meta "$SOURCE_HOME" source-home
    create_mock_commands "$MOCKBIN"

    local mount_unit
    mount_unit="$(systemd-escape -p --suffix=mount "$MOCK_MOUNTPOINT")"
    cat > "$CONFIG_FILE" <<CONFIG
BACKUP_MAPPER_NAME=$MAPPER_NAME
PROFILE_ID=default
PROFILE_NAME='Default backup'
BACKUP_DEVICE=$MOCK_DEVICE_LINK
BACKUP_LUKS_UUID=$MOCK_LUKS_UUID
BACKUP_BTRFS_UUID=$MOCK_TARGET_UUID
BACKUP_MOUNTPOINT=$MOCK_MOUNTPOINT
BACKUP_MOUNT_UNIT='$mount_unit'
BACKUP_SERVICE_NAME=btrfs-backup.service
SOURCES_DIR=$SOURCES_DIR
REMOTE_ROOT=$MOCK_MOUNTPOINT/snapshots
INCOMING_ROOT=$MOCK_MOUNTPOINT/.incoming
RETENTION_COUNT=2
LOCAL_RETENTION_COUNT=2
DAILY_LIMIT=true
INCREMENTAL_REQUIRED=true
KEEP_FAILED_LOCAL_SNAPSHOT=false
AUTO_EJECT=true
MIN_TARGET_FREE_BYTES=0
MIN_LOCAL_FREE_BYTES=0
LOCK_FILE=$LOCK_FILE
STATE_DIR=$STATE_DIR
STATUS_ROOT=$STATUS_ROOT
HISTORY_ROOT=$HISTORY_ROOT
EJECT_SCRIPT_PATH=$ROOT/scripts/btrfs-backup-eject.sh
NOTIFY_ENABLE=false
NOTIFY_USER=root
NOTIFY_METHOD=none
CONFIG
    chmod 0600 "$CONFIG_FILE"

    cat > "$SOURCES_DIR/10-root.conf" <<CONFIG
ENABLED=true
SOURCE_NAME=root
SOURCE_SUBVOLUME=$SOURCE_ROOT
LOCAL_SNAPSHOT_DIR=$LOCAL_ROOT
REMOTE_SUBDIR=root
SOURCE_RETENTION_COUNT=2
SOURCE_LOCAL_RETENTION_COUNT=2
CONFIG
    cat > "$SOURCES_DIR/20-home.conf" <<CONFIG
ENABLED=true
SOURCE_NAME=home
SOURCE_SUBVOLUME=$SOURCE_HOME
LOCAL_SNAPSHOT_DIR=$LOCAL_HOME
REMOTE_SUBDIR=home
SOURCE_RETENTION_COUNT=2
SOURCE_LOCAL_RETENTION_COUNT=2
CONFIG
    chmod 0600 "$SOURCES_DIR"/*.conf
    cat > "$PROFILE_JSON_FILE" <<JSON
{
  "schemaVersion": 1,
  "profileId": "default",
  "name": "Default backup",
  "enabled": true,
  "target": {
    "device": "$MOCK_DEVICE_LINK",
    "luksUuid": "$MOCK_LUKS_UUID",
    "btrfsUuid": "$MOCK_TARGET_UUID",
    "mapperName": "$MAPPER_NAME",
    "mountPoint": "$MOCK_MOUNTPOINT"
  },
  "paths": {
    "remoteRoot": "$MOCK_MOUNTPOINT/snapshots",
    "incomingRoot": "$MOCK_MOUNTPOINT/.incoming",
    "stateDir": "$STATE_DIR",
    "statusRoot": "$STATUS_ROOT",
    "historyRoot": "$HISTORY_ROOT"
  },
  "settings": {
    "remoteRetention": 2,
    "localRetention": 2,
    "minimumTargetFreeBytes": 0,
    "minimumLocalFreeBytes": 0
  },
  "sources": [
    {
      "id": "root",
      "name": "root",
      "enabled": true,
      "subvolume": "$SOURCE_ROOT",
      "localSnapshotDir": "$LOCAL_ROOT",
      "remoteSubdir": "root",
      "remoteRetention": 2,
      "localRetention": 2
    },
    {
      "id": "home",
      "name": "home",
      "enabled": true,
      "subvolume": "$SOURCE_HOME",
      "localSnapshotDir": "$LOCAL_HOME",
      "remoteSubdir": "home",
      "remoteRetention": 2,
      "localRetention": 2
    }
  ]
}
JSON
    chmod 0600 "$PROFILE_JSON_FILE"

    export MOCK_LOG MOCK_MOUNTPOINT MOCK_PHYSICAL_DEVICE MOCK_DM_DEVICE MOCK_MAPPER_NAME="$MAPPER_NAME"
    export MOCK_MAPPER_ROOT MOCK_LUKS_UUID MOCK_TARGET_UUID MOCK_SOURCE_UUID PROFILE_JSON_FILE
}

run_backup() {
    env \
        PATH="$MOCKBIN:$PATH" \
        INVOCATION_ID=test-invocation \
        MOCK_LOG="$MOCK_LOG" \
        MOCK_MOUNTPOINT="$MOCK_MOUNTPOINT" \
        MOCK_PHYSICAL_DEVICE="$MOCK_PHYSICAL_DEVICE" \
        MOCK_DM_DEVICE="$MOCK_DM_DEVICE" \
        MOCK_MAPPER_ROOT="$MOCK_MAPPER_ROOT" \
        MOCK_MAPPER_NAME="$MAPPER_NAME" \
        MOCK_LUKS_UUID="$MOCK_LUKS_UUID" \
        MOCK_TARGET_UUID="$MOCK_TARGET_UUID" \
        MOCK_SOURCE_UUID="$MOCK_SOURCE_UUID" \
        BTRFS_BACKUP_DEV_MAPPER_ROOT="$MOCK_MAPPER_ROOT" \
        BTRFS_BACKUP_LOCK_FILE="$LOCK_FILE" \
        BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS=true \
        BTRFS_BACKUP_DISABLE_NOTIFY=true \
        BTRFS_BACKUP_PROFILE_JSON="$PROFILE_JSON_FILE" \
        "${EXTRA_ENV[@]}" \
        "$ROOT/scripts/btrfs-backup.sh" "$@"
}

run_backup_profile() {
    local profile_config_dir="$1"
    shift

    env \
        PATH="$MOCKBIN:$PATH" \
        INVOCATION_ID=test-invocation \
        BTRFS_BACKUP_PROFILE_CONFIG_DIR="$profile_config_dir" \
        MOCK_LOG="$MOCK_LOG" \
        MOCK_MOUNTPOINT="$MOCK_MOUNTPOINT" \
        MOCK_PHYSICAL_DEVICE="$MOCK_PHYSICAL_DEVICE" \
        MOCK_DM_DEVICE="$MOCK_DM_DEVICE" \
        MOCK_MAPPER_ROOT="$MOCK_MAPPER_ROOT" \
        MOCK_MAPPER_NAME="$MAPPER_NAME" \
        MOCK_LUKS_UUID="$MOCK_LUKS_UUID" \
        MOCK_TARGET_UUID="$MOCK_TARGET_UUID" \
        MOCK_SOURCE_UUID="$MOCK_SOURCE_UUID" \
        BTRFS_BACKUP_DEV_MAPPER_ROOT="$MOCK_MAPPER_ROOT" \
        BTRFS_BACKUP_LOCK_FILE="$LOCK_FILE" \
        BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS=true \
        BTRFS_BACKUP_DISABLE_NOTIFY=true \
        "${EXTRA_ENV[@]}" \
        "$ROOT/scripts/btrfs-backup.sh" "$@"
}

profile_loading_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()
    local profile_dir="$RUNTIME/config/profiles.d"
    local empty_profile_dir="$RUNTIME/config/empty-profiles.d"
    "$ROOT/bin/btrfs-backupctl" profile create \
        --output "$RUNTIME/config/profiles/default/profile.json" \
        --profile default \
        --name 'Default backup' \
        --device "/dev/disk/by-uuid/$MOCK_LUKS_UUID" \
        --luks-uuid "$MOCK_LUKS_UUID" \
        --btrfs-uuid "$MOCK_TARGET_UUID" \
        --mapper-name "$MAPPER_NAME" \
        --mount-point "$MOCK_MOUNTPOINT" \
        --remote-root "$MOCK_MOUNTPOINT/snapshots" \
        --incoming-root "$MOCK_MOUNTPOINT/.incoming" \
        --state-dir "$STATE_DIR" \
        --status-root "$STATUS_ROOT" \
        --history-root "$HISTORY_ROOT" \
        --remote-retention 2 \
        --local-retention 2 \
        --minimum-target-free-bytes 0 \
        --minimum-local-free-bytes 0 \
        --notify-enable false \
        --notify-method none \
        --source root root "$SOURCE_ROOT" "$LOCAL_ROOT" root 2 2 \
        --source home home "$SOURCE_HOME" "$LOCAL_HOME" home 2 2 >/dev/null

    assert_file "$RUNTIME/config/profiles/default/profile.json"

    local profile_list
    profile_list="$("$ROOT/bin/btrfs-backupctl" \
        --profile-dir "$profile_dir" \
        profile list)"
    grep -qx default <<< "$profile_list" \
        || fail 'btrfs-backupctl did not list default profile'

    run_backup_profile "$profile_dir" --profile default --validate --no-eject >/dev/null
    assert_contains "$STATUS_ROOT/default/current.json" '"state": "validated"'

    install -d -m0700 "$RUNTIME/config/profiles/mismatch"
    cp -- "$RUNTIME/config/profiles/default/profile.json" "$RUNTIME/config/profiles/mismatch/profile.json"
    perl -0pi -e 's#"profileId": "default"#"profileId": "other"#' "$RUNTIME/config/profiles/mismatch/profile.json"
    chmod 0600 "$RUNTIME/config/profiles/mismatch/profile.json"
    if run_backup_profile "$profile_dir" --profile mismatch --validate --no-eject >/dev/null 2>&1; then
        fail 'profile id mismatch was accepted'
    fi

    pass 'runtime loads profile files'
}

runtime_success_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()

    run_backup
    assert_file "$PROFILE_STATE_DIR/last-success"
    assert_contains "$PROFILE_STATE_DIR/last-success" 'profile_id=default'
    [[ "$(stat -c '%a' "$STATE_DIR")" == 755 ]] || fail 'state root should be traversable for public history'
    [[ "$(stat -c '%a' "$PROFILE_STATE_DIR")" == 700 ]] || fail 'profile private state should remain root-only'
    assert_file "$STATUS_ROOT/default/current.json"
    assert_file "$HISTORY_ROOT/default/last.json"
    assert_contains "$STATUS_ROOT/default/current.json" '"state": "succeeded"'
    assert_contains "$HISTORY_ROOT/default/last.json" '"runId":'
    "$ROOT/bin/btrfs-backupctl" \
        --status-root "$STATUS_ROOT" \
        --history-root "$HISTORY_ROOT" \
        status show --profile default --human \
        | grep -q 'Default backup: succeeded' \
        || fail 'btrfs-backupctl did not render human status'
    local ctl_history_output
    ctl_history_output="$("$ROOT/bin/btrfs-backupctl" \
        --status-root "$STATUS_ROOT" \
        --history-root "$HISTORY_ROOT" \
        status history --profile default --limit 1)"
    grep -q '"state": "succeeded"' <<< "$ctl_history_output" \
        || fail 'btrfs-backupctl did not render history'
    if grep -qx ',' <<< "$ctl_history_output"; then
        fail 'btrfs-backupctl rendered a comma on a separate history line'
    fi
    assert_contains "$MOCK_LOG" 'SEND_FULL'
    assert_dir "$MOCK_MOUNTPOINT/snapshots/root"
    assert_dir "$MOCK_MOUNTPOINT/snapshots/home"
    [[ "$(find "$LOCAL_ROOT" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 1 ]] || fail 'expected one local root snapshot'
    [[ "$(find "$MOCK_MOUNTPOINT/snapshots/root" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 1 ]] || fail 'expected one remote root snapshot'

    local sends_before
    sends_before="$(grep -c '^SEND_' "$MOCK_LOG")"
    run_backup
    [[ "$(grep -c '^SEND_' "$MOCK_LOG")" -eq "$sends_before" ]] || fail 'daily limit did not skip second run'
    assert_contains "$STATUS_ROOT/default/current.json" '"state": "skipped"'

    printf '\n ' >> "$PROFILE_JSON_FILE"
    run_backup
    [[ "$(grep -c '^SEND_' "$MOCK_LOG")" -gt "$sends_before" ]] || fail 'configuration fingerprint did not invalidate the daily limit'
    assert_contains "$MOCK_LOG" 'SEND_INCREMENTAL'

    run_backup --force
    [[ "$(find "$LOCAL_ROOT" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 2 ]] || fail 'local retention did not keep two root snapshots'
    [[ "$(find "$MOCK_MOUNTPOINT/snapshots/root" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 2 ]] || fail 'remote retention did not keep two root snapshots'
    pass 'mocked full, daily, incremental, multi-source, and retention flow'
}

runtime_failure_cleanup_test() {
    prepare_runtime_fixture
    EXTRA_ENV=(MOCK_RECEIVE_FAIL=1)
    if run_backup --force >/dev/null 2>&1; then
        fail 'backup unexpectedly succeeded while receive was forced to fail'
    fi
    [[ "$(find "$LOCAL_ROOT" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 0 ]] || fail 'failed local snapshot was not removed'
    [[ -z "$(find "$MOCK_MOUNTPOINT/.incoming" -mindepth 1 -type d -name 'root-*' -print -quit 2>/dev/null)" ]] || fail 'partial receive was not removed'
    [[ -z "$(find "$PROFILE_STATE_DIR" -maxdepth 1 -name 'pending-*' -print -quit)" ]] || fail 'pending marker was not removed after handled failure'
    pass 'failed receive cleans local and incoming snapshots'
}

pending_recovery_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()
    local orphan="$LOCAL_ROOT/root-2026-01-01T000000Z"
    write_meta "$orphan" orphan-uuid
    mkdir -p "$PROFILE_STATE_DIR"
    cat > "$PROFILE_STATE_DIR/pending-root" <<PENDING
source_name=root
local_snapshot_path=$orphan
run_id=crashed
PENDING
    chmod 0600 "$PROFILE_STATE_DIR/pending-root"

    run_backup --force
    assert_not_exists "$orphan"
    assert_not_exists "$STATE_DIR/pending-root"
    assert_not_exists "$PROFILE_STATE_DIR/pending-root"
    assert_contains "$MOCK_LOG" "DELETE $orphan"
    pass 'pending recovery resolves an orphan from an unclean interruption'
}


pending_committed_recovery_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()
    local committed="$LOCAL_ROOT/root-2026-01-01T010000Z"
    write_meta "$committed" committed-uuid
    mkdir -p "$MOCK_MOUNTPOINT/snapshots/root"
    write_meta "$MOCK_MOUNTPOINT/snapshots/root/root-2026-01-01T010000Z" remote-committed committed-uuid
    mkdir -p "$PROFILE_STATE_DIR"
    cat > "$PROFILE_STATE_DIR/pending-root" <<PENDING
source_name=root
local_snapshot_path=$committed
run_id=crashed-after-commit
PENDING
    chmod 0600 "$PROFILE_STATE_DIR/pending-root"

    run_backup --force
    assert_dir "$committed"
    assert_not_exists "$PROFILE_STATE_DIR/pending-root"
    pass 'pending recovery preserves a local parent whose UUID is already committed remotely'
}

target_loss_recovery_test() {
    prepare_runtime_fixture
    EXTRA_ENV=(MOCK_RECEIVE_FAIL=1 MOCK_RECEIVE_UNMOUNT=1)
    if run_backup --force >/dev/null 2>&1; then
        fail 'backup unexpectedly succeeded during simulated target loss'
    fi
    [[ "$(find "$LOCAL_ROOT" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 1 ]] || fail 'local snapshot was not preserved after target loss'
    assert_file "$PROFILE_STATE_DIR/pending-root"

    EXTRA_ENV=()
    run_backup --force >/dev/null
    assert_not_exists "$PROFILE_STATE_DIR/pending-root"
    [[ "$(find "$MOCK_MOUNTPOINT/.incoming" -mindepth 1 -type d -name 'root-*' -print -quit 2>/dev/null)" == "" ]] || fail 'stale incoming data survived recovery'
    pass 'target loss preserves recovery state and the next run resolves it safely'
}

trusted_config_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()
    chmod 0644 "$PROFILE_JSON_FILE"
    if run_backup --force >/dev/null 2>&1; then
        fail 'world-readable profile JSON was accepted'
    fi
    pass 'runtime rejects active JSON configuration that is not root-only'
}

same_filesystem_rejected_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()
    MOCK_SOURCE_UUID="$MOCK_TARGET_UUID"
    if run_backup --force >/dev/null 2>&1; then
        fail 'source on the target Btrfs filesystem was accepted'
    fi
    pass 'runtime rejects a source that belongs to the backup target filesystem'
}

remote_symlink_escape_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()
    local escaped="$RUNTIME/escaped-remote"
    mkdir -p "$MOCK_MOUNTPOINT/snapshots" "$escaped"
    ln -s "$escaped" "$MOCK_MOUNTPOINT/snapshots/root"
    if run_backup --force >/dev/null 2>&1; then
        fail 'remote directory symlink escaping the target root was accepted'
    fi
    [[ -z "$(find "$escaped" -mindepth 1 -print -quit)" ]] \
        || fail 'backup wrote through an escaping remote symlink'
    pass 'runtime rejects remote paths that escape through a symlink'
}

eject_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()
    touch "$MOCK_MOUNTPOINT/.mock-mounted"
    ln -sfn "$MOCK_DM_DEVICE" "$MAPPER_PATH"

    env \
        PATH="$MOCKBIN:$PATH" \
        MOCK_LOG="$MOCK_LOG" \
        MOCK_MOUNTPOINT="$MOCK_MOUNTPOINT" \
        MOCK_PHYSICAL_DEVICE="$MOCK_PHYSICAL_DEVICE" \
        MOCK_DM_DEVICE="$MOCK_DM_DEVICE" \
        MOCK_MAPPER_ROOT="$MOCK_MAPPER_ROOT" \
        MOCK_MAPPER_NAME="$MAPPER_NAME" \
        MOCK_LUKS_UUID="$MOCK_LUKS_UUID" \
        MOCK_TARGET_UUID="$MOCK_TARGET_UUID" \
        MOCK_SOURCE_UUID="$MOCK_SOURCE_UUID" \
        BTRFS_BACKUP_DEV_MAPPER_ROOT="$MOCK_MAPPER_ROOT" \
        BTRFS_BACKUP_LOCK_FILE="$LOCK_FILE" \
        BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS=true \
        BTRFS_BACKUP_DISABLE_NOTIFY=true \
        BTRFS_BACKUP_PROFILE_JSON="$PROFILE_JSON_FILE" \
        "$ROOT/scripts/btrfs-backup-eject.sh" >/dev/null

    assert_not_exists "$MOCK_MOUNTPOINT/.mock-mounted"
    assert_not_exists "$MAPPER_PATH"
    assert_contains "$MOCK_LOG" "UMOUNT $MOCK_MOUNTPOINT"
    assert_contains "$MOCK_LOG" "CRYPT_CLOSE $MAPPER_NAME"
    pass 'explicit eject syncs, unmounts, and closes the expected mapper'
}

if [[ "$MODE" == static ]]; then
    printf '1..4\n'
    syntax_test
    render_test
    profile_json_test
    status_writer_cli_test
    exit 0
fi

printf '1..14\n'
syntax_test
render_test
profile_json_test
status_writer_cli_test
profile_loading_test
runtime_success_test
runtime_failure_cleanup_test
pending_recovery_test
pending_committed_recovery_test
target_loss_recovery_test
trusted_config_test
same_filesystem_rejected_test
remote_symlink_escape_test
eject_test
