#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-real.XXXXXX)"
SOURCE_IMAGE="$TEST_ROOT/source.img"
TARGET_IMAGE="$TEST_ROOT/target.img"
SOURCE_LOOP=""
TARGET_LOOP=""
PARTITION_NODE_MONITOR_PID=""
SOURCE_MOUNT=/mnt/bb-real-source
TARGET_MOUNT=/mnt/btrfs-backup/default
TARGET_STAGING_MOUNT=/mnt/bb-real-target-staging
MAPPER_NAME="bb-real-target-${TEST_ROOT##*.}"
MAPPER_PATH="/dev/mapper/$MAPPER_NAME"
PASSPHRASE_FILE="$TEST_ROOT/luks.pass"
PACKAGE_DIR="${BTRFSBACKUP_PACKAGE_DIR:-$TEST_ROOT/package}"
LOG_DIR="$TEST_ROOT/logs"
PROFILE_JSON=/etc/btrfs-backup/profiles/default/profile.json
BROWSE_SESSION_CLIENT="${BTRFSBACKUP_BROWSE_SESSION_CLIENT:?missing browse-session integration client}"
DEVICE_PROVISIONING_CLIENT="${BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT:?missing device-provisioning integration client}"
REAL_BTRFS_TESTS="${BTRFSBACKUP_REAL_BTRFS_TESTS:?missing real-Btrfs C++ integration test}"
REAL_INSTALLED_RUNTIME_TESTS="${BTRFSBACKUP_REAL_INSTALLED_RUNTIME_TESTS:?missing installed-runtime C++ integration test}"
REAL_MAPPER_LIFECYCLE_TESTS="${BTRFSBACKUP_REAL_MAPPER_LIFECYCLE_TESTS:?missing mapper-lifecycle C++ integration test}"
REAL_TRUSTED_HOOK_TESTS="${BTRFSBACKUP_REAL_TRUSTED_HOOK_TESTS:?missing trusted-hook C++ integration test}"
REAL_SANDBOXED_SYSTEMD_TESTS="${BTRFSBACKUP_REAL_SANDBOXED_SYSTEMD_TESTS:?missing sandboxed-systemd C++ integration test}"
REAL_SYSTEM_DBUS_BACKUP_TESTS="${BTRFSBACKUP_REAL_SYSTEM_DBUS_BACKUP_TESTS:?missing system-D-Bus-backup C++ integration test}"

cleanup() {
    set +e
    if [[ -n "$PARTITION_NODE_MONITOR_PID" ]]; then
        kill "$PARTITION_NODE_MONITOR_PID" 2>/dev/null
        wait "$PARTITION_NODE_MONITOR_PID" 2>/dev/null
    fi
    umount -R "$TARGET_MOUNT" 2>/dev/null
    umount -R "$TARGET_STAGING_MOUNT" 2>/dev/null
    for _ in $(seq 1 5); do
        timeout --kill-after=1s 1s cryptsetup close "$MAPPER_NAME" 2>/dev/null && break
        sleep 0.1
    done
    umount -R "$SOURCE_MOUNT" 2>/dev/null
    [[ -n "$TARGET_LOOP" ]] && losetup -d "$TARGET_LOOP" 2>/dev/null
    [[ -n "$SOURCE_LOOP" ]] && losetup -d "$SOURCE_LOOP" 2>/dev/null
    rm -rf -- \
        "$TEST_ROOT" \
        "$SOURCE_MOUNT" \
        "$TARGET_MOUNT" \
        "$TARGET_STAGING_MOUNT"
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
    for index in $(seq 0 63); do
        if [[ ! -e "/dev/loop$index" ]]; then
            mknod "/dev/loop$index" b 7 "$index"
        fi
    done
}

materialize_loop_device_nodes() {
    local sysfs_device device_name major minor actual_major actual_minor temporary_node
    install -d -m0755 /dev/block
    for sysfs_device in /sys/class/block/loop*; do
        [[ -r "$sysfs_device/dev" ]] || continue
        device_name="${sysfs_device##*/}"
        IFS=: read -r major minor < "$sysfs_device/dev"
        actual_major="$(stat -c '%t' "/dev/$device_name" 2>/dev/null || true)"
        actual_minor="$(stat -c '%T' "/dev/$device_name" 2>/dev/null || true)"
        if [[ "$actual_major" != "$(printf '%x' "$major")" ||
              "$actual_minor" != "$(printf '%x' "$minor")" ]]; then
            temporary_node="/dev/.${device_name}.${BASHPID}.tmp"
            mknod "$temporary_node" b "$major" "$minor"
            mv -f -- "$temporary_node" "/dev/$device_name"
        fi
        ln -sfn "../$device_name" "/dev/block/$major:$minor"
    done
}

materialize_device_mapper_nodes() {
    local sysfs_device device_name mapper_name major minor actual_major actual_minor temporary_node
    install -d -m0755 /dev/block /dev/mapper
    for sysfs_device in /sys/class/block/dm-*; do
        [[ -r "$sysfs_device/dev" && -r "$sysfs_device/dm/name" ]] || continue
        IFS= read -r mapper_name < "$sysfs_device/dm/name" 2>/dev/null || continue
        [[ "$mapper_name" =~ ^[A-Za-z0-9_.+-]+$ ]] || continue
        [[ "$mapper_name" == btrfs-backup-* || "$mapper_name" == bb-real-* ]] || continue
        device_name="${sysfs_device##*/}"
        IFS=: read -r major minor < "$sysfs_device/dev"
        actual_major="$(stat -c '%t' "/dev/$device_name" 2>/dev/null || true)"
        actual_minor="$(stat -c '%T' "/dev/$device_name" 2>/dev/null || true)"
        if [[ "$actual_major" != "$(printf '%x' "$major")" ||
              "$actual_minor" != "$(printf '%x' "$minor")" ]]; then
            temporary_node="/dev/.${device_name}.${BASHPID}.tmp"
            mknod "$temporary_node" b "$major" "$minor"
            mv -f -- "$temporary_node" "/dev/$device_name"
        fi
        ln -sfn "../$device_name" "/dev/mapper/$mapper_name"
        ln -sfn "../$device_name" "/dev/block/$major:$minor"
    done
}

materialize_loop_identity_links() {
    local sysfs_device device_name filesystem_uuid partition_uuid
    install -d -m0755 /dev/disk/by-uuid /dev/disk/by-partuuid
    for sysfs_device in /sys/class/block/loop*p*; do
        device_name="${sysfs_device##*/}"
        [[ -b "/dev/$device_name" ]] || continue
        filesystem_uuid="$(blkid -p -s UUID -o value "/dev/$device_name" 2>/dev/null || true)"
        partition_uuid="$(blkid -p -s PART_ENTRY_UUID -o value "/dev/$device_name" 2>/dev/null || true)"
        if [[ -n "$filesystem_uuid" ]]; then
            ln -sfn "../../$device_name" "/dev/disk/by-uuid/$filesystem_uuid"
        fi
        if [[ -n "$partition_uuid" ]]; then
            ln -sfn "../../$device_name" "/dev/disk/by-partuuid/$partition_uuid"
        fi
    done
}

monitor_loop_partition_nodes() {
    while true; do
        materialize_loop_device_nodes || true
        materialize_device_mapper_nodes || true
        materialize_loop_identity_links || true
        sleep 0.05
    done
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

manager_independence_test() {
    local hook_dir=/etc/btrfs-backup/hooks.d
    local hook="$hook_dir/manager-independence-test"
    local marker="$TEST_ROOT/manager-independence.marker"
    local release_fifo="$TEST_ROOT/manager-independence.release"
    local profile_backup="$TEST_ROOT/profile.manager-independence.json.bak"
    local runner_log="$LOG_DIR/manager-independence.log"
    local runner_pid

    cp -a -- "$PROFILE_JSON" "$profile_backup"
    perl -0pi -e \
        's#"beforeSnapshot": \[\]#"beforeSnapshot": [{"type":"program","program":"/etc/btrfs-backup/hooks.d/manager-independence-test","arguments":[],"timeoutSeconds":30}]#' \
        "$PROFILE_JSON"
    chmod 0600 "$PROFILE_JSON"
    mkfifo -m0600 "$release_fifo"
    {
        printf '#!/bin/sh\n'
        printf "printf 'started\\n' > '%s'\n" "$marker"
        printf "IFS= read -r release < '%s'\n" "$release_fifo"
        printf "printf 'finished\\n' > '%s'\n" "$marker"
    } > "$hook"
    chmod 0755 "$hook"
    chown root:root "$hook"

    systemctl reload dbus.service
    systemctl start btrfs-backupd.service
    systemctl is-active --quiet btrfs-backupd.service \
        || fail 'system manager did not start'

    (
        INVOCATION_ID=manager-independence-test \
            btrfs-backup --force --no-eject >> "$runner_log" 2>&1
    ) &
    runner_pid=$!
    for _ in $(seq 1 50); do
        [[ -f "$marker" ]] && break
        kill -0 "$runner_pid" 2>/dev/null || break
        sleep 0.1
    done
    [[ "$(cat -- "$marker" 2>/dev/null || true)" == 'started' ]] \
        || { cat -- "$runner_log" >&2; fail 'runner did not enter the blocking hook'; }

    systemctl stop btrfs-backupd.service
    systemctl is-active --quiet btrfs-backupd.service \
        && fail 'system manager remained active after stop'
    kill -0 "$runner_pid" 2>/dev/null \
        || { cat -- "$runner_log" >&2; fail 'stopping the manager terminated the active runner'; }
    printf 'continue\n' > "$release_fifo"
    if ! wait "$runner_pid"; then
        cat -- "$runner_log" >&2
        fail 'runner failed after the manager was stopped'
    fi

    [[ "$(cat -- "$marker")" == 'finished' ]] \
        || fail 'runner did not complete the blocking hook'
    grep -q '"state": "succeeded"' /var/lib/btrfs-backup/history/default/last.json \
        || fail 'runner did not persist successful history after manager stop'
    assert_remote_matches_latest_local

    cp -a -- "$profile_backup" "$PROFILE_JSON"
    rm -f -- "$profile_backup" "$hook" "$marker" "$release_fifo"
    pass 'active runner completes after the system manager stops'
}

require_root
require_commands awk blkid btrfs busctl cat cmp cryptsetup date dd diff dmsetup find findmnt grep journalctl ldd ln losetup mkfifo mkfs.btrfs mkfs.ext4 mknod mount mv pacman perl runuser seq sfdisk sha256sum stat systemd-escape systemd-run tar tee timeout truncate udevadm useradd userdel
[[ -x "$BROWSE_SESSION_CLIENT" ]] || fail 'browse-session integration client is not executable'
[[ -x "$DEVICE_PROVISIONING_CLIENT" ]] || fail 'device-provisioning integration client is not executable'
[[ -x "$REAL_BTRFS_TESTS" ]] || fail 'real-Btrfs C++ integration test is not executable'
[[ -x "$REAL_INSTALLED_RUNTIME_TESTS" ]] || fail 'installed-runtime C++ integration test is not executable'
[[ -x "$REAL_MAPPER_LIFECYCLE_TESTS" ]] || fail 'mapper-lifecycle C++ integration test is not executable'
[[ -x "$REAL_TRUSTED_HOOK_TESTS" ]] || fail 'trusted-hook C++ integration test is not executable'
[[ -x "$REAL_SANDBOXED_SYSTEMD_TESTS" ]] || fail 'sandboxed-systemd C++ integration test is not executable'
[[ -x "$REAL_SYSTEM_DBUS_BACKUP_TESTS" ]] || fail 'system-D-Bus-backup C++ integration test is not executable'
ensure_loop_devices
for _ in $(seq 1 100); do
    systemctl show-environment >/dev/null 2>&1 && break
    sleep 0.1
done
systemctl show-environment >/dev/null 2>&1 \
    || fail 'systemd did not become ready before provisioning setup'
systemctl start systemd-udevd.service
systemctl is-active --quiet systemd-udevd.service \
    || fail 'systemd-udevd did not start before provisioning loop devices'
# shellcheck disable=SC2016 # udev expands $kernel when handling the device event.
printf '%s\n' \
    'SUBSYSTEM=="block", KERNEL=="loop*", ENV{ID_BUS}="usb", ENV{ID_SERIAL}="btrfs-backup-test-$kernel"' \
    > /etc/udev/rules.d/98-btrfs-backup-loop-test.rules
udevadm control --reload
monitor_loop_partition_nodes &
PARTITION_NODE_MONITOR_PID=$!

install -d -m0755 "$SOURCE_MOUNT" "$TARGET_MOUNT"
install -d -m0700 "$LOG_DIR"
"$REAL_BTRFS_TESTS" \
    /usr/bin/btrfs-backupctl \
    "$BROWSE_SESSION_CLIENT" \
    "$DEVICE_PROVISIONING_CLIENT" \
    "$PACKAGE_DIR" \
    "$ROOT"

printf '%s\n' 'btrfs-backup-real-test-passphrase' > "$PASSPHRASE_FILE"
chmod 0600 "$PASSPHRASE_FILE"

truncate -s 768M "$SOURCE_IMAGE"
truncate -s 768M "$TARGET_IMAGE"
SOURCE_LOOP="$(losetup --find --show "$SOURCE_IMAGE")"
TARGET_LOOP="$(losetup --find --show "$TARGET_IMAGE")"

mkfs.btrfs -q -f "$SOURCE_LOOP"
cryptsetup luksFormat \
    --batch-mode \
    --type luks2 \
    --pbkdf pbkdf2 \
    --pbkdf-force-iterations 1000 \
    --key-file "$PASSPHRASE_FILE" \
    "$TARGET_LOOP"
cryptsetup open --key-file "$PASSPHRASE_FILE" "$TARGET_LOOP" "$MAPPER_NAME"
udevadm settle --timeout=10
dmsetup mknodes "$MAPPER_NAME"
[[ -b "$MAPPER_PATH" ]] || fail "cryptsetup mapper was not created: $MAPPER_PATH"
mkfs.btrfs -q -f "$MAPPER_PATH"

mount -o noatime,compress=zstd:3 "$SOURCE_LOOP" "$SOURCE_MOUNT"
btrfs subvolume create "$SOURCE_MOUNT/home" >/dev/null
mount --bind "$SOURCE_MOUNT/home" "$SOURCE_MOUNT/home"
install -d -m0700 "$SOURCE_MOUNT/.snapshots/home"

mount -o noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd:3 "$MAPPER_PATH" "$TARGET_MOUNT"
install -d -m0700 "$TARGET_MOUNT/snapshots" "$TARGET_MOUNT/.incoming"

printf 'alpha\n' > "$SOURCE_MOUNT/home/file-a.txt"
install -d -m0755 "$SOURCE_MOUNT/home/dir"
dd if=/dev/urandom of="$SOURCE_MOUNT/home/dir/blob.bin" bs=1M count=2 status=none
sync -f "$SOURCE_MOUNT"

"$REAL_MAPPER_LIFECYCLE_TESTS" "$TARGET_MOUNT" "$TARGET_LOOP" "$MAPPER_NAME" "$PASSPHRASE_FILE"

"$REAL_INSTALLED_RUNTIME_TESTS" \
    /usr/bin/btrfs-backupctl \
    /usr/bin/btrfs-backup \
    "$TEST_ROOT" \
    "$SOURCE_MOUNT" \
    "$TARGET_MOUNT" \
    "$TARGET_LOOP" \
    "$MAPPER_NAME" \
    "$PASSPHRASE_FILE"
"$REAL_SYSTEM_DBUS_BACKUP_TESTS" "$SOURCE_MOUNT" "$TARGET_MOUNT" "$TEST_ROOT"
manager_independence_test
"$REAL_TRUSTED_HOOK_TESTS" /usr/bin/btrfs-backup "$PROFILE_JSON" "$TEST_ROOT"
"$REAL_MAPPER_LIFECYCLE_TESTS" "$TARGET_MOUNT" "$TARGET_LOOP" "$MAPPER_NAME" "$PASSPHRASE_FILE"
"$REAL_SANDBOXED_SYSTEMD_TESTS" \
    "$SOURCE_MOUNT" \
    "$TARGET_MOUNT" \
    "$TARGET_STAGING_MOUNT" \
    "$MAPPER_NAME" \
    "$PROFILE_JSON"

printf 'Real Btrfs integration test completed in %s\n' "$TEST_ROOT"
