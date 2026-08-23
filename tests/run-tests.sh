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

  --full         Run syntax, render, status and mount/eject compatibility tests (default).
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
        --backup-command "$ROOT/bin/btrfs-backupctl runner execute" \
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

printf '1..5\n'
syntax_test
render_test
profile_json_test
status_writer_cli_test
eject_test
