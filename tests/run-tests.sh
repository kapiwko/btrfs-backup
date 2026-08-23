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

  --full         Run all mocked runtime tests; requires root (default).
  --static-only  Run syntax and rendering validation without /dev access.
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
MAPPER_PATH="/dev/mapper/$MAPPER_NAME"
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
    mapfile -t scripts < <(find "$ROOT" -type f \( -name '*.sh' -o -name '*.install' -o \( -path "$ROOT/bin/*" ! -path "$ROOT/bin/__pycache__/*" \) \) | sort)
    local script
    for script in "${scripts[@]}"; do
        if head -n1 "$script" | grep -q 'python3'; then
            python3 - "$script" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
compile(path.read_text(encoding="utf-8"), str(path), "exec")
PY
        else
            bash -n "$script"
        fi
    done
    pass 'all Bash files parse'
}

render_test() {
    local output="$TEST_ROOT/rendered"
    local answers="$TEST_ROOT/answers.sh"
    cat > "$answers" <<ANSWERS
BACKUP_DEVICE=/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555
PROFILE_ID=laptop
PROFILE_NAME='Laptop backup'
BACKUP_LUKS_UUID=11111111-2222-3333-4444-555555555555
BACKUP_UDEV_MATCH='ENV{DEVTYPE}=="partition", ENV{ID_FS_TYPE}=="crypto_LUKS", ENV{ID_FS_UUID}=="11111111-2222-3333-4444-555555555555", ENV{ID_PART_ENTRY_UUID}=="aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"'
BACKUP_MAPPER_NAME=backupdisk
BACKUP_MOUNTPOINT=/mnt/backup
BACKUP_BTRFS_UUID=66666666-7777-8888-9999-aaaaaaaaaaaa
KEYFILE_PATH_OR_NONE=/root/keys/backupdisk.key
NOTIFY_USER=tester
SOURCE_SUBVOLUMES=(/ /home)
SOURCE_NAMES=(root home)
LOCAL_SNAPSHOT_DIRS=(/.snapshots/btrfs-backup/root /.snapshots/btrfs-backup/home)
REMOTE_SUBDIRS=(root home)
SOURCE_RETENTION_COUNTS=(30 45)
SOURCE_LOCAL_RETENTION_COUNTS=(30 20)
ANSWERS
    chmod 0600 "$answers"
    make_invoker_owned "$answers"

    "$ROOT/install/btrfs-backup-configure.sh" \
        --answers "$answers" \
        --template-dir "$ROOT" \
        --output-dir "$output" \
        --render-only >/dev/null

    assert_file "$output/config/backup.env"
    assert_file "$output/config/profile.json"
    assert_file "$output/config/profiles.d/laptop.env"
    assert_contains "$output/config/backup.env" "PROFILE_ID=laptop"
    assert_contains "$output/config/profiles.d/laptop.env" "PROFILE_ID=laptop"
    assert_contains "$output/config/backup.env" "PROFILE_NAME='Laptop backup'"
    assert_contains "$output/config/profiles.d/laptop.env" "SOURCES_DIR=/etc/btrfs-backup/profiles/laptop/sources.d"
    assert_file "$output/config/profiles/laptop/sources.d/010-root.conf"
    assert_file "$output/config/profiles/laptop/sources.d/020-home.conf"
    assert_contains "$output/config/profile.json" '"profileId": "laptop"'
    assert_file "$output/systemd/btrfs-backup.service"
    assert_file "$output/systemd/btrfs-backup@.service"
    assert_file "$output/udev/99-btrfs-backup.rules"
    assert_not_contains "$output/udev/99-btrfs-backup.rules" 'ACTION=="remove"'
    assert_contains "$output/udev/99-btrfs-backup.rules" 'btrfs-backup@laptop.service'
    assert_not_contains "$output/systemd/btrfs-backup.service" 'WantedBy='
    assert_not_contains "$output/systemd/btrfs-backup.service" 'Requires=mnt-backup.mount'
    assert_contains "$output/systemd/btrfs-backup.service" 'ExecStart='
    assert_contains "$output/systemd/btrfs-backup.service" '--profile laptop'
    assert_contains "$output/config/fstab.fragment" 'noauto'
    assert_contains "$output/config/fstab.fragment" 'x-systemd.requires=systemd-cryptsetup@backupdisk.service'
    if grep -R -q '{{' "$output"; then
        fail 'rendered output contains unresolved placeholders'
    fi
    pass 'configurator renders validated multi-source configuration'
}

migrate_profile_dry_run_test() {
    local source_config="$TEST_ROOT/legacy-backup.env"
    local source_dir="$TEST_ROOT/legacy-sources.d"
    local profile_dir="$TEST_ROOT/profiles.d"
    local legacy_only_profile_dir="$TEST_ROOT/legacy-only-profiles.d"
    local udev_dir="$TEST_ROOT/udev"
    local public_dir="$TEST_ROOT/public"

    mkdir -p "$source_dir" "$profile_dir" "$legacy_only_profile_dir" "$udev_dir" "$public_dir"
    cat > "$source_config" <<CONFIG
BACKUP_MAPPER_NAME=backupdisk
BACKUP_DEVICE=/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555
BACKUP_LUKS_UUID=11111111-2222-3333-4444-555555555555
BACKUP_BTRFS_UUID=66666666-7777-8888-9999-aaaaaaaaaaaa
BACKUP_MOUNTPOINT=/mnt/backup
SOURCES_DIR=$source_dir
RETENTION_COUNT=30
LOCAL_RETENTION_COUNT=20
DAILY_LIMIT=true
INCREMENTAL_REQUIRED=true
KEEP_FAILED_LOCAL_SNAPSHOT=false
AUTO_EJECT=true
MIN_TARGET_FREE_BYTES=0
MIN_LOCAL_FREE_BYTES=0
NOTIFY_ENABLE=false
NOTIFY_USER=tester
NOTIFY_METHOD=none
CONFIG
    cat > "$source_dir/10-home.conf" <<CONFIG
ENABLED=true
SOURCE_NAME=home
SOURCE_SUBVOLUME=/home
LOCAL_SNAPSHOT_DIR=/.snapshots/btrfs-backup/home
REMOTE_SUBDIR=home
SOURCE_RETENTION_COUNT=45
SOURCE_LOCAL_RETENTION_COUNT=20
CONFIG
    chmod 0600 "$source_config" "$source_dir/10-home.conf"
    "$ROOT/bin/btrfs-backup-migrate-profile" \
        --dry-run \
        --source "$source_config" \
        --profile-dir "$profile_dir" \
        --profile default >/dev/null
    "$ROOT/bin/btrfs-backup-migrate-profile" \
        --source "$source_config" \
        --profile-dir "$profile_dir" \
        --udev-dir "$udev_dir" \
        --public-dir "$public_dir" \
        --profile default \
        --name 'Default backup' >/dev/null
    assert_file "$profile_dir/default.env"
    assert_file "$TEST_ROOT/profiles/default/profile.json"
    assert_file "$TEST_ROOT/profiles/default/sources.d/010-home.conf"
    assert_file "$udev_dir/99-btrfs-backup-default.rules"
    assert_file "$public_dir/default.json"
    assert_contains "$profile_dir/default.env" "SOURCES_DIR=$TEST_ROOT/profiles/default/sources.d"
    assert_contains "$TEST_ROOT/profiles/default/profile.json" '"profileId": "default"'
    assert_contains "$TEST_ROOT/profiles/default/sources.d/010-home.conf" 'SOURCE_NAME=home'

    printf 'PROFILE_ID=laptop\n' > "$profile_dir/laptop.env"
    local profile_list
    profile_list="$("$ROOT/bin/btrfs-backupctl" \
        --profile-dir "$profile_dir" \
        --legacy-config "$source_config" \
        list-profiles)"
    grep -qx laptop <<< "$profile_list" \
        || fail 'btrfs-backupctl did not list profile files'
    rm -f -- "$profile_dir/laptop.env"
    profile_list="$("$ROOT/bin/btrfs-backupctl" \
        --profile-dir "$legacy_only_profile_dir" \
        --legacy-config "$source_config" \
        list-profiles)"
    grep -qx 'default (legacy)' <<< "$profile_list" \
        || fail 'btrfs-backupctl did not list legacy default profile'

    local remove_config="$TEST_ROOT/remove-legacy-backup.env"
    local remove_source_dir="$TEST_ROOT/remove-legacy-sources.d"
    mkdir -p "$remove_source_dir"
    cp -- "$source_config" "$remove_config"
    sed -i "s|^SOURCES_DIR=.*|SOURCES_DIR=$remove_source_dir|" "$remove_config"
    cp -- "$source_dir/10-home.conf" "$remove_source_dir/10-home.conf"
    chmod 0600 "$remove_config" "$remove_source_dir/10-home.conf"
    "$ROOT/bin/btrfs-backup-migrate-profile" \
        --source "$remove_config" \
        --profile-dir "$profile_dir" \
        --udev-dir "$udev_dir" \
        --public-dir "$public_dir" \
        --profile removelegacy \
        --remove-legacy >/dev/null
    assert_file "$profile_dir/removelegacy.env"
    assert_not_exists "$remove_config"
    assert_not_exists "$remove_source_dir"
    remove_config_backups=("$TEST_ROOT"/remove-legacy-backup.env.migrated-*)
    remove_source_backups=("$TEST_ROOT"/remove-legacy-sources.d.migrated-*)
    (( ${#remove_config_backups[@]} == 1 )) || fail 'legacy config was not moved aside'
    (( ${#remove_source_backups[@]} == 1 )) || fail 'legacy sources.d was not moved aside'
    assert_file "${remove_config_backups[0]}"
    assert_dir "${remove_source_backups[0]}"
    assert_file "${remove_source_backups[0]}/10-home.conf"
    pass 'profile migrator validates and materializes JSON runtime files'
}

profile_json_test() {
    local rendered="$TEST_ROOT/profile-json-rendered"
    local saved="$TEST_ROOT/profile-json-saved"

    "$ROOT/bin/btrfs-backup-profile" validate --file "$ROOT/config/profile.example.json" >/dev/null
    "$ROOT/bin/btrfs-backup-profile" render \
        --file "$ROOT/config/profile.example.json" \
        --output-dir "$rendered" >/dev/null

    assert_file "$rendered/etc/btrfs-backup/profiles.d/default.env"
    assert_file "$rendered/etc/btrfs-backup/profiles/default/sources.d/010-home.conf"
    assert_file "$rendered/etc/udev/rules.d/99-btrfs-backup-default.rules"
    assert_file "$rendered/var/lib/btrfs-backup/public/profiles/default.json"
    assert_contains "$rendered/etc/btrfs-backup/profiles.d/default.env" 'PROFILE_ID=default'
    assert_contains "$rendered/etc/btrfs-backup/profiles/default/sources.d/010-home.conf" 'SOURCE_NAME=home'
    assert_contains "$rendered/etc/udev/rules.d/99-btrfs-backup-default.rules" 'btrfs-backup@default.service'

    "$ROOT/bin/btrfs-backup-profile" \
        --etc-root "$saved/etc/btrfs-backup" \
        --udev-root "$saved/etc/udev/rules.d" \
        --public-root "$saved/var/lib/btrfs-backup/public/profiles" \
        save --file "$ROOT/config/profile.example.json" >/dev/null

    assert_file "$saved/etc/btrfs-backup/profiles.d/default.env"
    assert_file "$saved/etc/btrfs-backup/profiles/default/sources.d/010-home.conf"
    assert_file "$saved/etc/udev/rules.d/99-btrfs-backup-default.rules"
    assert_file "$saved/var/lib/btrfs-backup/public/profiles/default.json"
    "$ROOT/bin/btrfs-backup-profile" \
        --etc-root "$saved/etc/btrfs-backup" \
        show --profile default > "$saved/show.json"
    assert_contains "$saved/show.json" '"profileId": "default"'
    "$ROOT/bin/btrfs-backup-profile" \
        --etc-root "$saved/etc/btrfs-backup" \
        export --profile default --output "$saved/exported.json" >/dev/null
    assert_file "$saved/exported.json"
    assert_contains "$saved/exported.json" '"profileId": "default"'
    pass 'profile JSON validates, renders, and saves generated runtime files'
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
        mkdir -p -- "$MOCK_MOUNTPOINT" /dev/mapper
        touch "$MOCK_MOUNTPOINT/.mock-mounted"
        ln -sfn "$MOCK_DM_DEVICE" "/dev/mapper/$MOCK_MAPPER_NAME"
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
        printf '/dev/mapper/%s %s\n' "$MOCK_MAPPER_NAME" "$MOCK_MOUNTPOINT"
    fi
    exit 0
fi
if [[ "$mode" == M ]]; then
    case "$output" in
        SOURCE) printf '/dev/mapper/%s\n' "$MOCK_MAPPER_NAME" ;;
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
        rm -f -- "/dev/mapper/${2:-$MOCK_MAPPER_NAME}"
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
    MOCK_PHYSICAL_DEVICE="$RUNTIME/dev/sdb1"
    MOCK_DM_DEVICE="$RUNTIME/dev/dm-0"
    MOCK_DEVICE_LINK="$RUNTIME/dev/disk/by-uuid/test-luks"
    MOCK_LUKS_UUID=11111111-2222-3333-4444-555555555555
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
    CONFIG_FILE="$RUNTIME/config/backup.env"

    rm -rf -- "$RUNTIME"
    mkdir -p "$MOCKBIN" "$RUNTIME/dev/disk/by-uuid" "$RUNTIME/dev" "$SOURCES_DIR" \
        "$STATE_DIR" "$SOURCE_ROOT" "$SOURCE_HOME" "$LOCAL_ROOT" "$LOCAL_HOME" "$MOCK_MOUNTPOINT" /dev/mapper
    : > "$MOCK_PHYSICAL_DEVICE"
    : > "$MOCK_DM_DEVICE"
    ln -s "$MOCK_PHYSICAL_DEVICE" "$MOCK_DEVICE_LINK"
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

    export MOCK_LOG MOCK_MOUNTPOINT MOCK_PHYSICAL_DEVICE MOCK_DM_DEVICE MOCK_MAPPER_NAME="$MAPPER_NAME"
    export MOCK_LUKS_UUID MOCK_TARGET_UUID MOCK_SOURCE_UUID
}

run_backup() {
    env \
        PATH="$MOCKBIN:$PATH" \
        INVOCATION_ID=test-invocation \
        MOCK_LOG="$MOCK_LOG" \
        MOCK_MOUNTPOINT="$MOCK_MOUNTPOINT" \
        MOCK_PHYSICAL_DEVICE="$MOCK_PHYSICAL_DEVICE" \
        MOCK_DM_DEVICE="$MOCK_DM_DEVICE" \
        MOCK_MAPPER_NAME="$MAPPER_NAME" \
        MOCK_LUKS_UUID="$MOCK_LUKS_UUID" \
        MOCK_TARGET_UUID="$MOCK_TARGET_UUID" \
        MOCK_SOURCE_UUID="$MOCK_SOURCE_UUID" \
        "${EXTRA_ENV[@]}" \
        "$ROOT/scripts/btrfs-backup.sh" --config "$CONFIG_FILE" "$@"
}

run_backup_profile() {
    local profile_config_dir="$1"
    local legacy_config="$2"
    shift 2

    env \
        PATH="$MOCKBIN:$PATH" \
        INVOCATION_ID=test-invocation \
        BTRFS_BACKUP_PROFILE_CONFIG_DIR="$profile_config_dir" \
        BTRFS_BACKUP_LEGACY_CONFIG="$legacy_config" \
        MOCK_LOG="$MOCK_LOG" \
        MOCK_MOUNTPOINT="$MOCK_MOUNTPOINT" \
        MOCK_PHYSICAL_DEVICE="$MOCK_PHYSICAL_DEVICE" \
        MOCK_DM_DEVICE="$MOCK_DM_DEVICE" \
        MOCK_MAPPER_NAME="$MAPPER_NAME" \
        MOCK_LUKS_UUID="$MOCK_LUKS_UUID" \
        MOCK_TARGET_UUID="$MOCK_TARGET_UUID" \
        MOCK_SOURCE_UUID="$MOCK_SOURCE_UUID" \
        "${EXTRA_ENV[@]}" \
        "$ROOT/scripts/btrfs-backup.sh" "$@"
}

profile_loading_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()
    local profile_dir="$RUNTIME/config/profiles.d"
    local migrated="$profile_dir/default.env"
    local empty_profile_dir="$RUNTIME/config/empty-profiles.d"
    local migration_config="$RUNTIME/config/migration.env"

    cp -- "$CONFIG_FILE" "$migration_config"
    sed -i "s|^BACKUP_DEVICE=.*|BACKUP_DEVICE=/dev/disk/by-uuid/$MOCK_LUKS_UUID|" "$migration_config"
    chmod 0600 "$migration_config"
    "$ROOT/scripts/btrfs-backup-migrate-profile.sh" \
        --source "$migration_config" \
        --sources-dir "$SOURCES_DIR" \
        --profile-dir "$profile_dir" \
        --udev-dir "$RUNTIME/udev" \
        --public-dir "$RUNTIME/public" \
        --profile default >/dev/null

    assert_file "$migrated"
    assert_contains "$migrated" 'PROFILE_ID=default'
    assert_contains "$migrated" "PROFILE_NAME='Default backup'"
    assert_file "$RUNTIME/config/profiles/default/profile.json"
    assert_file "$RUNTIME/config/profiles/default/sources.d/010-root.conf"
    sed -i "s|^BACKUP_DEVICE=.*|BACKUP_DEVICE=$MOCK_DEVICE_LINK|" "$migrated"

    local profile_list
    profile_list="$("$ROOT/bin/btrfs-backupctl" \
        --profile-dir "$profile_dir" \
        --legacy-config "$CONFIG_FILE" \
        list-profiles)"
    grep -qx default <<< "$profile_list" \
        || fail 'btrfs-backupctl did not list migrated default profile'

    run_backup_profile "$profile_dir" "$RUNTIME/config/missing.env" --profile default --validate --no-eject >/dev/null
    assert_contains "$STATUS_ROOT/default/current.json" '"state": "validated"'

    mkdir -p "$empty_profile_dir"
    local fallback_log="$RUNTIME/fallback.log"
    run_backup_profile "$empty_profile_dir" "$CONFIG_FILE" --profile default --validate --no-eject >"$fallback_log" 2>&1
    assert_contains "$STATUS_ROOT/default/current.json" '"state": "validated"'
    grep -q 'Legacy configuration fallback is deprecated' "$fallback_log" \
        || fail 'legacy fallback did not emit a deprecation warning'

    cp -- "$CONFIG_FILE" "$profile_dir/mismatch.env"
    printf '\nPROFILE_ID=other\n' >> "$profile_dir/mismatch.env"
    chmod 0600 "$profile_dir/mismatch.env"
    if run_backup_profile "$profile_dir" "$RUNTIME/config/missing.env" --profile mismatch --validate --no-eject >/dev/null 2>&1; then
        fail 'profile id mismatch was accepted'
    fi

    pass 'runtime loads profile files and preserves legacy fallback'
}

runtime_success_test() {
    prepare_runtime_fixture
    EXTRA_ENV=()

    run_backup
    assert_file "$PROFILE_STATE_DIR/last-success"
    assert_contains "$PROFILE_STATE_DIR/last-success" 'profile_id=default'
    assert_file "$STATUS_ROOT/default/current.json"
    assert_file "$HISTORY_ROOT/default/last.json"
    assert_contains "$STATUS_ROOT/default/current.json" '"state": "succeeded"'
    assert_contains "$HISTORY_ROOT/default/last.json" '"runId":'
    "$ROOT/bin/btrfs-backupctl" \
        --status-root "$STATUS_ROOT" \
        --history-root "$HISTORY_ROOT" \
        status --profile default --human \
        | grep -q 'Default backup: succeeded' \
        || fail 'btrfs-backupctl did not render human status'
    local ctl_history_output
    ctl_history_output="$("$ROOT/bin/btrfs-backupctl" \
        --status-root "$STATUS_ROOT" \
        --history-root "$HISTORY_ROOT" \
        history --profile default --limit 1)"
    grep -q '"state": "succeeded"' <<< "$ctl_history_output" \
        || fail 'btrfs-backupctl did not render history'
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

    printf '\n# configuration change must invalidate the daily success state\n' >> "$SOURCES_DIR/20-home.conf"
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
    cat > "$STATE_DIR/pending-root" <<PENDING
source_name=root
local_snapshot_path=$orphan
run_id=crashed
PENDING
    chmod 0600 "$STATE_DIR/pending-root"

    run_backup --force
    assert_not_exists "$orphan"
    assert_not_exists "$STATE_DIR/pending-root"
    assert_not_exists "$PROFILE_STATE_DIR/pending-root"
    assert_contains "$MOCK_LOG" "DELETE $orphan"
    pass 'legacy pending marker migrates and recovers an orphan from an unclean interruption'
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
    chmod 0644 "$CONFIG_FILE"
    if run_backup --force >/dev/null 2>&1; then
        fail 'world-readable main configuration was accepted'
    fi

    chmod 0600 "$CONFIG_FILE"
    chmod 0644 "$SOURCES_DIR/10-root.conf"
    if run_backup --force >/dev/null 2>&1; then
        fail 'world-readable source configuration was accepted'
    fi
    pass 'runtime rejects active shell configuration that is not root-only'
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
        MOCK_MAPPER_NAME="$MAPPER_NAME" \
        MOCK_LUKS_UUID="$MOCK_LUKS_UUID" \
        MOCK_TARGET_UUID="$MOCK_TARGET_UUID" \
        MOCK_SOURCE_UUID="$MOCK_SOURCE_UUID" \
        "$ROOT/scripts/btrfs-backup-eject.sh" --config "$CONFIG_FILE" >/dev/null

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
    migrate_profile_dry_run_test
    profile_json_test
    exit 0
fi

if (( EUID != 0 )); then
    fail 'full mocked runtime tests require root; use --static-only otherwise'
fi

printf '1..14\n'
syntax_test
render_test
migrate_profile_dry_run_test
profile_json_test
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
