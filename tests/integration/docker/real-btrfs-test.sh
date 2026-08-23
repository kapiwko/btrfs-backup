#!/usr/bin/env bash
set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-real.XXXXXX)"
SOURCE_IMAGE="$TEST_ROOT/source.img"
TARGET_IMAGE="$TEST_ROOT/target.img"
SOURCE_LOOP=""
TARGET_LOOP=""
SOURCE_MOUNT=/mnt/bb-real-source
TARGET_MOUNT=/mnt/bb-real-target
MAPPER_NAME=bb-real-target
MAPPER_PATH="/dev/mapper/$MAPPER_NAME"
PASSPHRASE_FILE="$TEST_ROOT/luks.pass"
PACKAGE_DIR="$TEST_ROOT/package"
RENDERED_CONFIG="$TEST_ROOT/rendered"
LOG_DIR="$TEST_ROOT/logs"
RUN_LOG="$LOG_DIR/btrfs-backup.log"

cleanup() {
    set +e
    umount -R "$TARGET_MOUNT" 2>/dev/null
    cryptsetup close "$MAPPER_NAME" 2>/dev/null
    umount -R "$SOURCE_MOUNT" 2>/dev/null
    [[ -n "$TARGET_LOOP" ]] && losetup -d "$TARGET_LOOP" 2>/dev/null
    [[ -n "$SOURCE_LOOP" ]] && losetup -d "$SOURCE_LOOP" 2>/dev/null
    rm -rf -- "$TEST_ROOT" "$SOURCE_MOUNT" "$TARGET_MOUNT"
}
trap cleanup EXIT

fail() {
    printf 'not ok - %s\n' "$1" >&2
    exit 1
}

pass() {
    printf 'ok - %s\n' "$1"
}

require_root() {
    if (( EUID != 0 )); then
        fail 'real Btrfs integration test must run as root'
    fi
}

require_commands() {
    local missing=()
    local command_name
    for command_name in "$@"; do
        command -v "$command_name" >/dev/null 2>&1 || missing+=("$command_name")
    done
    if (( ${#missing[@]} > 0 )); then
        fail "missing commands: ${missing[*]}"
    fi
}

ensure_loop_devices() {
    local index

    if [[ ! -e /dev/loop-control ]]; then
        mknod /dev/loop-control c 10 237
    fi
    for index in $(seq 0 15); do
        if [[ ! -e "/dev/loop$index" ]]; then
            mknod "/dev/loop$index" b 7 "$index"
        fi
    done
}

build_and_install_package() {
    "$ROOT/tools/build-release.sh" --target arch --skip-tests --dist-dir "$PACKAGE_DIR" >/dev/null
    pacman -U --noconfirm "$PACKAGE_DIR"/btrfs-backup-*-any.pkg.tar.zst >/dev/null
    command -v btrfs-backup >/dev/null
    command -v btrfs-backup-configure >/dev/null
}

configure_backup_with_cli() {
    local target_device="$1"
    local luks_uuid="$2"
    local btrfs_uuid="$3"
    local answers="$TEST_ROOT/configurator-answers.conf"

    cat > "$answers" <<ANSWERS
BACKUP_DEVICE=$target_device
BACKUP_LUKS_UUID=$luks_uuid
BACKUP_BTRFS_UUID=$btrfs_uuid
BACKUP_UDEV_MATCH='ENV{DEVTYPE}=="disk", ENV{ID_FS_TYPE}=="crypto_LUKS", ENV{ID_FS_UUID}=="$luks_uuid"'
BACKUP_MAPPER_NAME=$MAPPER_NAME
BACKUP_MOUNTPOINT=$TARGET_MOUNT
KEYFILE_PATH_OR_NONE=none
SOURCE_SUBVOLUMES=($SOURCE_MOUNT/home)
SOURCE_NAMES=(home)
LOCAL_SNAPSHOT_DIRS=($SOURCE_MOUNT/.snapshots/home)
REMOTE_SUBDIRS=(home)
SOURCE_RETENTION_COUNTS=(2)
SOURCE_LOCAL_RETENTION_COUNTS=(2)
RETENTION_COUNT=2
LOCAL_RETENTION_COUNT=2
DAILY_LIMIT=false
INCREMENTAL_REQUIRED=true
KEEP_FAILED_LOCAL_SNAPSHOT=false
AUTO_EJECT=false
MIN_TARGET_FREE_BYTES=0
MIN_LOCAL_FREE_BYTES=0
NOTIFY_ENABLE=false
NOTIFY_USER=root
NOTIFY_METHOD=none
ANSWERS
    chmod 0600 "$answers"
    chown root:root "$answers"

    btrfs-backup-configure \
        --render-only \
        --answers "$answers" \
        --output-dir "$RENDERED_CONFIG" >/dev/null
    btrfs-backup-configure --validate-dir "$RENDERED_CONFIG" >/dev/null

    install -d -m0700 /etc/btrfs-backup /etc/btrfs-backup/sources.d
    install -m0600 "$RENDERED_CONFIG/config/backup.env" /etc/btrfs-backup/backup.env
    rm -f -- /etc/btrfs-backup/sources.d/*.conf
    install -m0600 "$RENDERED_CONFIG/config/sources.d"/*.conf /etc/btrfs-backup/sources.d/
    install -Dm0644 "$RENDERED_CONFIG/systemd/btrfs-backup.service" /etc/systemd/system/btrfs-backup.service
    install -Dm0644 "$RENDERED_CONFIG/udev/99-btrfs-backup.rules" /etc/udev/rules.d/99-btrfs-backup.rules
    btrfs-backup-configure --validate >/dev/null
}

run_backup() {
    INVOCATION_ID=real-docker-test \
        btrfs-backup \
        --force \
        --no-eject 2>&1 | tee -a "$RUN_LOG"
}

expect_backup_failure() {
    local pattern="$1"
    local output

    set +e
    output="$(INVOCATION_ID=real-docker-test btrfs-backup --force --no-eject 2>&1)"
    local status=$?
    set -e

    [[ "$status" -ne 0 ]] || fail "backup unexpectedly succeeded; expected: $pattern"
    grep -Fq -- "$pattern" <<< "$output" || fail "backup failed without expected message: $pattern"
}

with_restored_file() {
    local file="$1"
    shift
    local backup="$TEST_ROOT/$(basename -- "$file").bak.$$"

    cp -a -- "$file" "$backup"
    "$@"
    cp -a -- "$backup" "$file"
    rm -f -- "$backup"
}

assert_count() {
    local expected="$1"
    local directory="$2"
    local actual

    actual="$(find "$directory" -mindepth 1 -maxdepth 1 -type d -name 'home-*' | wc -l)"
    [[ "$actual" -eq "$expected" ]] || fail "expected $expected snapshots in $directory, got $actual"
}

assert_remote_matches_latest_local() {
    local latest_local latest_remote local_uuid received_uuid

    latest_local="$(find "$SOURCE_MOUNT/.snapshots/home" -mindepth 1 -maxdepth 1 -type d -name 'home-*' | sort | tail -n1)"
    latest_remote="$(find "$TARGET_MOUNT/snapshots/home" -mindepth 1 -maxdepth 1 -type d -name 'home-*' | sort | tail -n1)"
    [[ -n "$latest_local" && -n "$latest_remote" ]] || fail 'latest local or remote snapshot not found'

    if ! diff -qr "$latest_local" "$latest_remote" >/dev/null; then
        diff -qr "$latest_local" "$latest_remote" >&2 || true
        fail 'latest remote snapshot does not match latest local snapshot'
    fi

    local_uuid="$(btrfs subvolume show "$latest_local" | sed -n 's/^[[:space:]]*UUID:[[:space:]]*//p' | head -n1)"
    received_uuid="$(btrfs subvolume show "$latest_remote" | sed -n 's/^[[:space:]]*Received UUID:[[:space:]]*//p' | head -n1)"
    [[ -n "$local_uuid" && "${local_uuid,,}" == "${received_uuid,,}" ]] \
        || fail 'remote Received UUID does not match latest local snapshot UUID'
}

assert_no_incoming_children() {
    local leftover

    leftover="$(find "$TARGET_MOUNT/.incoming/home" -mindepth 1 -print -quit 2>/dev/null || true)"
    [[ -z "$leftover" ]] || fail 'incoming source directory is not empty after successful backups'
}

validate_runtime_preflight() {
    INVOCATION_ID=real-docker-test btrfs-backup --validate --no-eject >/dev/null
}

target_uuid_mismatch_test() {
    sed -i "s/^BACKUP_BTRFS_UUID=.*/BACKUP_BTRFS_UUID=00000000-0000-0000-0000-000000000000/" \
        /etc/btrfs-backup/backup.env
    expect_backup_failure 'Btrfs UUID mismatch'
    pass 'runtime rejects a mismatched target Btrfs UUID'
}

source_on_target_test() {
    btrfs subvolume create "$TARGET_MOUNT/bad-source" >/dev/null
    install -d -m0700 "$TARGET_MOUNT/.bad-local"
    cat > /etc/btrfs-backup/sources.d/10-home.conf <<CONFIG
ENABLED=true
SOURCE_NAME=home
SOURCE_SUBVOLUME=$TARGET_MOUNT/bad-source
LOCAL_SNAPSHOT_DIR=$TARGET_MOUNT/.bad-local
REMOTE_SUBDIR=home
SOURCE_RETENTION_COUNT=2
SOURCE_LOCAL_RETENTION_COUNT=2
CONFIG
    chmod 0600 /etc/btrfs-backup/sources.d/10-home.conf
    expect_backup_failure 'SOURCE_SUBVOLUME must not be on the backup target filesystem'
    btrfs subvolume delete -- "$TARGET_MOUNT/bad-source" >/dev/null
    rm -rf -- "$TARGET_MOUNT/.bad-local"
    pass 'runtime rejects a source on the backup target filesystem'
}

missing_incremental_parent_test() {
    local empty_local_dir="$SOURCE_MOUNT/.snapshots/empty-parent-check"
    local source_config=/etc/btrfs-backup/sources.d/10-home.conf
    local source_config_backup="$TEST_ROOT/10-home.conf.parent-check.bak"

    cp -a -- "$source_config" "$source_config_backup"
    install -d -m0700 "$empty_local_dir"
    sed -i "s|^LOCAL_SNAPSHOT_DIR=.*|LOCAL_SNAPSHOT_DIR=$empty_local_dir|" "$source_config"
    printf 'delta\n' > "$SOURCE_MOUNT/home/orphan-parent-check.txt"
    sync
    expect_backup_failure 'Remote snapshots exist for home, but no UUID-matching local parent was found.'
    find "$empty_local_dir" -mindepth 1 -maxdepth 1 -type d -name 'home-*' -exec btrfs subvolume delete -- {} \; >/dev/null
    rmdir -- "$empty_local_dir"
    cp -a -- "$source_config_backup" "$source_config"
    rm -f -- "$source_config_backup"
    pass 'runtime rejects incremental backup when remote snapshots exist without a local parent'
}

require_root
require_commands btrfs cryptsetup dd diff find findmnt losetup mkfs.btrfs mknod mount pacman seq sha256sum systemd-escape tee truncate
ensure_loop_devices

install -d -m0755 "$SOURCE_MOUNT" "$TARGET_MOUNT"
install -d -m0700 "$LOG_DIR"
build_and_install_package
pass 'package builds and installs'

printf '%s\n' 'btrfs-backup-real-test-passphrase' > "$PASSPHRASE_FILE"
chmod 0600 "$PASSPHRASE_FILE"

truncate -s 768M "$SOURCE_IMAGE"
truncate -s 768M "$TARGET_IMAGE"
SOURCE_LOOP="$(losetup --find --show "$SOURCE_IMAGE")"
TARGET_LOOP="$(losetup --find --show "$TARGET_IMAGE")"

mkfs.btrfs -q -f "$SOURCE_LOOP"
cryptsetup luksFormat --batch-mode --type luks2 --key-file "$PASSPHRASE_FILE" "$TARGET_LOOP"
cryptsetup open --key-file "$PASSPHRASE_FILE" "$TARGET_LOOP" "$MAPPER_NAME"
mkfs.btrfs -q -f "$MAPPER_PATH"

mount -o noatime,compress=zstd:3 "$SOURCE_LOOP" "$SOURCE_MOUNT"
btrfs subvolume create "$SOURCE_MOUNT/home" >/dev/null
install -d -m0700 "$SOURCE_MOUNT/.snapshots/home"

mount -o noatime,compress=zstd:3 "$MAPPER_PATH" "$TARGET_MOUNT"
install -d -m0700 "$TARGET_MOUNT/snapshots" "$TARGET_MOUNT/.incoming"

printf 'alpha\n' > "$SOURCE_MOUNT/home/file-a.txt"
install -d -m0755 "$SOURCE_MOUNT/home/dir"
dd if=/dev/urandom of="$SOURCE_MOUNT/home/dir/blob.bin" bs=1M count=8 status=none
sync

TARGET_LUKS_UUID="$(cryptsetup luksUUID "$TARGET_LOOP")"
TARGET_BTRFS_UUID="$(findmnt -n -o UUID -M "$TARGET_MOUNT")"
configure_backup_with_cli "$TARGET_LOOP" "$TARGET_LUKS_UUID" "$TARGET_BTRFS_UUID"
pass 'installed CLI renders, installs, and validates configuration'
validate_runtime_preflight
pass 'installed runtime validates the mounted target'
with_restored_file /etc/btrfs-backup/backup.env target_uuid_mismatch_test
with_restored_file /etc/btrfs-backup/sources.d/10-home.conf source_on_target_test

run_backup
grep -q 'Sending full stream' "$RUN_LOG" || fail 'full stream was not used for first backup'
grep -q '^profile_id=default$' /var/lib/btrfs-backup/profiles/default/last-success \
    || fail 'profile last-success state was not written'
grep -q '"state": "succeeded"' /run/btrfs-backup/profiles/default/current.json \
    || fail 'current status JSON was not written'
grep -q '"state": "succeeded"' /var/lib/btrfs-backup/history/default/last.json \
    || fail 'history JSON was not written'
assert_count 1 "$TARGET_MOUNT/snapshots/home"
assert_count 1 "$SOURCE_MOUNT/.snapshots/home"
assert_remote_matches_latest_local
pass 'full backup transfers and verifies real Btrfs data'

printf 'beta\n' >> "$SOURCE_MOUNT/home/file-a.txt"
rm -f -- "$SOURCE_MOUNT/home/dir/blob.bin"
dd if=/dev/urandom of="$SOURCE_MOUNT/home/dir/blob-2.bin" bs=1M count=12 status=none
sync
run_backup
grep -q 'Sending incremental stream with parent' "$RUN_LOG" || fail 'incremental stream was not used for second backup'
assert_count 2 "$TARGET_MOUNT/snapshots/home"
assert_count 2 "$SOURCE_MOUNT/.snapshots/home"
assert_remote_matches_latest_local
pass 'incremental backup transfers and verifies real Btrfs data'
missing_incremental_parent_test

printf 'gamma\n' > "$SOURCE_MOUNT/home/new-file.txt"
dd if=/dev/urandom of="$SOURCE_MOUNT/home/dir/blob-3.bin" bs=1M count=4 status=none
sync
run_backup
assert_count 2 "$TARGET_MOUNT/snapshots/home"
assert_count 2 "$SOURCE_MOUNT/.snapshots/home"
assert_remote_matches_latest_local
assert_no_incoming_children
pass 'retention keeps the latest two local and remote snapshots'

printf 'Real Btrfs integration test completed in %s\n' "$TEST_ROOT"
