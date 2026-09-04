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
RUN_LOG="$LOG_DIR/btrfs-backup.log"
PROFILE_JSON=/etc/btrfs-backup/profiles/default/profile.json
EJECT_COMPLETION_COUNT=0
BROWSE_SESSION_CLIENT="${BTRFSBACKUP_BROWSE_SESSION_CLIENT:?missing browse-session integration client}"
DEVICE_PROVISIONING_CLIENT="${BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT:?missing device-provisioning integration client}"
REAL_BTRFS_TESTS="${BTRFSBACKUP_REAL_BTRFS_TESTS:?missing real-Btrfs C++ integration test}"
REAL_INSTALLED_RUNTIME_TESTS="${BTRFSBACKUP_REAL_INSTALLED_RUNTIME_TESTS:?missing installed-runtime C++ integration test}"
REAL_MAPPER_LIFECYCLE_TESTS="${BTRFSBACKUP_REAL_MAPPER_LIFECYCLE_TESTS:?missing mapper-lifecycle C++ integration test}"

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

run_backup() {
    INVOCATION_ID=real-docker-test \
        btrfs-backup \
        --force \
        --no-eject 2>&1 | tee -a "$RUN_LOG"
}

run_first_backup_through_system_dbus() {
    local test_user=btrfs-dbus-test
    local action_id=io.github.btrfsbackup.start-backup
    local passwordless_action
    local policy_rule=/etc/polkit-1/rules.d/49-btrfs-backup-integration.rules
    local action_details
    local response
    local status_response
    local operation_id
    local unit
    local state

    action_details="$(pkaction --verbose --action-id "$action_id")" \
        || fail 'installed polkit policy does not register the start-backup action'
    grep -Eq 'implicit active:[[:space:]]+yes' <<< "$action_details" \
        || fail 'installed polkit policy does not permit passwordless start from an active session'
    for passwordless_action in \
        io.github.btrfsbackup.cancel-backup \
        io.github.btrfsbackup.validate-target \
        io.github.btrfsbackup.eject-target; do
        action_details="$(pkaction --verbose --action-id "$passwordless_action")" \
            || fail "installed polkit policy does not register $passwordless_action"
        grep -Eq 'implicit active:[[:space:]]+yes' <<< "$action_details" \
            || fail "installed polkit policy requires a password for $passwordless_action"
    done
    useradd --system --no-create-home --shell /usr/bin/nologin "$test_user"
    [[ "$(id -u "$test_user")" -ne 0 ]] || fail 'D-Bus integration caller unexpectedly has UID 0'
    cat > "$policy_rule" <<'EOF_POLKIT_RULE'
polkit.addRule(function(action, subject) {
    if (action.id == "io.github.btrfsbackup.start-backup" &&
        subject.user == "btrfs-dbus-test") {
        return polkit.Result.YES;
    }
});
EOF_POLKIT_RULE
    chmod 0644 "$policy_rule"

    rm -rf -- \
        /run/btrfs-backup/profiles/default \
        /var/lib/btrfs-backup/history/default
    systemctl reload dbus.service
    systemctl start polkit.service btrfs-backupd.service
    systemctl is-active --quiet polkit.service \
        || fail 'real polkit authority did not start'
    systemctl is-active --quiet btrfs-backupd.service \
        || fail 'system manager did not start'

    if ! response="$(runuser -u "$test_user" -- \
        busctl --system call \
            io.github.btrfsbackup.Manager1 \
            /io/github/btrfsbackup/Manager1 \
            io.github.btrfsbackup.Manager1 \
            StartBackup s default 2>&1)"; then
        journalctl --no-pager -u polkit.service -u btrfs-backupd.service -n 100 >&2 || true
        printf '%s\n' "$response" >&2
        fail 'unprivileged user could not request StartBackup through the system D-Bus API'
    fi
    grep -Eq '\\"accepted\\":[[:space:]]*true' <<< "$response" \
        || fail "manager did not accept the unprivileged start request: $response"
    operation_id="$(sed -n 's/.*\\"operationId\\":[[:space:]]*\\"\([^"\\]*\)\\".*/\1/p' <<< "$response")"
    [[ -n "$operation_id" ]] || fail "manager response omitted the operation id: $response"
    unit="btrfs-backup-run@$operation_id.service"

    for _ in $(seq 1 600); do
        if [[ -f /var/lib/btrfs-backup/history/default/last.json ]]; then
            state="$(sed -n 's/.*"state":[[:space:]]*"\([^"]*\)".*/\1/p' \
                /var/lib/btrfs-backup/history/default/last.json | head -n1)"
            [[ "$state" == 'succeeded' ]] && break
            [[ "$state" == 'failed' || "$state" == 'cancelled' ]] && break
        fi
        sleep 0.1
    done

    for _ in $(seq 1 300); do
        state="$(systemctl show --property=ActiveState --value "$unit" 2>/dev/null || true)"
        [[ -z "$state" || "$state" == 'inactive' || "$state" == 'failed' ]] && break
        sleep 0.1
    done

    journalctl --sync
    journalctl --no-pager -o cat -u "$unit" -u btrfs-backupd.service | tee -a "$RUN_LOG"
    state="$(sed -n 's/.*"state":[[:space:]]*"\([^"]*\)".*/\1/p' \
        /var/lib/btrfs-backup/history/default/last.json 2>/dev/null | head -n1)"
    if [[ "$state" != 'succeeded' ]]; then
        cat /var/lib/btrfs-backup/history/default/last.json >&2 2>/dev/null || true
        systemctl status --no-pager "$unit" >&2 || true
        journalctl --no-pager -u btrfs-backupd.service -n 100 >&2 || true
        fail "backup requested over D-Bus did not succeed (state: ${state:-missing})"
    fi
    status_response="$(runuser -u "$test_user" -- \
        busctl --system call \
            io.github.btrfsbackup.Manager1 \
            /io/github/btrfsbackup/Manager1 \
            io.github.btrfsbackup.Manager1 \
            GetStatus s default)"
    grep -Fq '\"state\": \"succeeded\"' <<< "$status_response" \
        || fail "manager did not reconstruct terminal status from history: $status_response"

    systemctl stop btrfs-backupd.service polkit.service
    rm -f -- "$policy_rule"
    userdel "$test_user"
    pass 'unprivileged user starts a real backup through system D-Bus and polkit'
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

trusted_hook_security_test() {
    local hook_dir=/etc/btrfs-backup/hooks.d
    local hook="$hook_dir/integration-test"
    local original_hook="$hook_dir/integration-test.original"
    local outside_hook="$TEST_ROOT/untrusted-hook"
    local marker="$TEST_ROOT/trusted-hook-ran"
    local profile_backup="$TEST_ROOT/profile.hook-security.json.bak"

    [[ "$(stat -c '%U:%G:%a' "$hook_dir")" == 'root:root:755' ]] \
        || fail 'package did not install /etc/btrfs-backup/hooks.d as root:root 0755'
    cp -a -- "$PROFILE_JSON" "$profile_backup"
    perl -0pi -e \
        's#"beforeSnapshot": \[\]#"beforeSnapshot": [{"type":"program","program":"/etc/btrfs-backup/hooks.d/integration-test","arguments":[],"timeoutSeconds":30}]#' \
        "$PROFILE_JSON"
    chmod 0600 "$PROFILE_JSON"

    {
        printf '#!/bin/sh\n'
        printf "printf 'trusted\\n' > '%s'\n" "$marker"
    } > "$hook"
    chmod 0755 "$hook"
    chown root:root "$hook"
    run_backup
    [[ "$(cat -- "$marker")" == 'trusted' ]] || fail 'trusted root-owned hook was not executed'

    chown 1000:1000 "$hook"
    expect_backup_failure 'hook program must be owned by root'
    chown root:root "$hook"

    chmod 0775 "$hook"
    expect_backup_failure 'hook program must not be writable by group or others'
    chmod 0755 "$hook"

    chmod 0777 "$hook_dir"
    expect_backup_failure 'trusted hook parent must not be writable by group or others'
    chmod 0755 "$hook_dir"

    cp -a -- "$hook" "$outside_hook"
    mv -- "$hook" "$original_hook"
    ln -s -- "$outside_hook" "$hook"
    expect_backup_failure 'Too many levels of symbolic links'
    rm -f -- "$hook"
    mv -- "$original_hook" "$hook"

    cp -a -- "$profile_backup" "$PROFILE_JSON"
    rm -f -- "$profile_backup" "$hook" "$outside_hook" "$marker"
    pass 'runtime executes only pinned root-owned hooks from trusted directories'
}

wait_for_eject_service() {
    local completion_count
    local state

    for _ in $(seq 1 100); do
        completion_count="$(journalctl --no-pager -u btrfs-backup-eject@default.service -o cat \
            | grep -Fc 'Finished Safely eject Btrfs backup target for profile default.' || true)"
        state="$(systemctl show -P ActiveState btrfs-backup-eject@default.service)"
        if (( completion_count > EJECT_COMPLETION_COUNT )) && [[ "$state" == 'inactive' ]]; then
            EJECT_COMPLETION_COUNT="$completion_count"
            return
        fi
        sleep 0.1
    done
    systemctl status --no-pager btrfs-backup-eject@default.service >&2 || true
    if [[ -e "$MAPPER_PATH" ]]; then
        local mapper_device
        mapper_device="$(lsblk -dnro MAJ:MIN "$MAPPER_PATH" 2>/dev/null || true)"
        dmsetup info -c --noheadings -o name,major,minor,open,segments,uuid "$MAPPER_NAME" >&2 \
            || true
        dmsetup ls --tree >&2 || true
        if [[ -n "$mapper_device" ]]; then
            local mount_info
            for mount_info in /proc/[0-9]*/mountinfo; do
                if grep -Fq " $mapper_device " "$mount_info" 2>/dev/null; then
                    local mount_pid="${mount_info#/proc/}"
                    mount_pid="${mount_pid%/mountinfo}"
                    printf '%s\n' "mapper $mapper_device is visible in $mount_info:" >&2
                    grep -F " $mapper_device " "$mount_info" >&2 || true
                    ps -o pid,ppid,user,stat,comm,args -p "$mount_pid" 2>/dev/null >&2 || true
                fi
            done
        fi
    fi
    fail 'timed out waiting for the target eject service'
}

sandboxed_systemd_service_test() {
    local mount_unit
    local mount_unit_path
    mount_unit="$(systemd-escape -p --suffix=mount "$TARGET_MOUNT")"
    mount_unit_path="/etc/systemd/system/$mount_unit"
    printf 'systemd sandbox\n' >> "$SOURCE_MOUNT/home/file-a.txt"
    sync -f "$SOURCE_MOUNT"
    install -d -m0755 "$TARGET_STAGING_MOUNT"
    mount --move "$TARGET_MOUNT" "$TARGET_STAGING_MOUNT"
    if findmnt -n -M "$TARGET_MOUNT" >/dev/null 2>&1; then
        fail 'target remained mounted before sandboxed service test'
    fi
    {
        printf '[Unit]\nDescription=Disposable bind mount for sandbox test\n\n'
        printf '[Mount]\nWhat=%s\nWhere=%s\nType=none\nOptions=bind\n' \
            "$TARGET_STAGING_MOUNT" "$TARGET_MOUNT"
    } > "$mount_unit_path"
    systemctl daemon-reload
    [[ "$(systemctl show -P NoNewPrivileges btrfs-backup@default.service)" == 'yes' ]] \
        || fail 'systemd did not apply NoNewPrivileges'
    [[ "$(systemctl show -P ProtectSystem btrfs-backup@default.service)" == 'full' ]] \
        || fail 'systemd did not apply ProtectSystem=full'
    [[ "$(systemctl show -P MemoryDenyWriteExecute btrfs-backup@default.service)" == 'yes' ]] \
        || fail 'systemd did not apply MemoryDenyWriteExecute'
    if ! systemctl start btrfs-backup@default.service; then
        systemctl status --no-pager btrfs-backup@default.service >&2 || true
        systemctl status --no-pager "$mount_unit" >&2 || true
        journalctl --no-pager -u btrfs-backup@default.service -n 100 >&2 || true
        fail 'sandboxed systemd service failed'
    fi
    wait_for_eject_service

    [[ "$(systemctl show -P Result btrfs-backup@default.service)" == 'success' ]] \
        || fail 'sandboxed systemd service did not finish successfully'
    findmnt -n -M "$TARGET_MOUNT" >/dev/null \
        || fail 'sandboxed runner did not start or observe the target mount unit'
    grep -q '"state": "succeeded"' /var/lib/btrfs-backup/history/default/last.json \
        || fail 'sandboxed service did not publish successful history'
    assert_remote_matches_latest_local
    assert_no_incoming_children
    pass 'sandboxed systemd service completes a real Btrfs backup'
}

sandboxed_auto_eject_test() {
    local mount_unit
    mount_unit="$(systemd-escape -p --suffix=mount "$TARGET_MOUNT")"

    systemctl stop "$mount_unit"
    rm -f -- "/etc/systemd/system/$mount_unit"
    printf '[Unit]\n' > /etc/systemd/system/btrfs-backup@default.service.d/target-mount.conf
    systemctl daemon-reload
    mount --move "$TARGET_STAGING_MOUNT" "$TARGET_MOUNT"
    sed -i 's/"autoEject": false/"autoEject": true/' "$PROFILE_JSON"
    printf 'automatic eject\n' >> "$SOURCE_MOUNT/home/file-a.txt"
    sync -f "$SOURCE_MOUNT"
    if ! systemctl start btrfs-backup@default.service; then
        systemctl status --no-pager btrfs-backup@default.service >&2 || true
        journalctl --no-pager -u btrfs-backup@default.service -n 100 >&2 || true
        fail 'sandboxed service failed before automatic eject'
    fi
    wait_for_eject_service
    [[ "$(systemctl show -P Result btrfs-backup-eject@default.service)" == 'success' ]] \
        || fail 'target eject service did not finish successfully'
    if findmnt -n -M "$TARGET_MOUNT" >/dev/null 2>&1; then
        fail 'target remained mounted after the eject service'
    fi
    for _ in $(seq 1 100); do
        if ! cryptsetup status "$MAPPER_NAME" >/dev/null 2>&1 && [[ ! -e "$MAPPER_PATH" ]]; then
            break
        fi
        sleep 0.05
    done
    if cryptsetup status "$MAPPER_NAME" >/dev/null 2>&1 || [[ -e "$MAPPER_PATH" ]]; then
        fail 'LUKS mapper remained open after the eject service'
    fi
    pass 'automatic eject runs outside the backup mount namespace'
}

require_root
require_commands awk blkid btrfs busctl cat cmp cryptsetup date dd diff dmsetup find findmnt grep journalctl ldd ln losetup mkfifo mkfs.btrfs mkfs.ext4 mknod mount mv pacman perl runuser seq sfdisk sha256sum stat systemd-escape systemd-run tar tee timeout truncate udevadm useradd userdel
[[ -x "$BROWSE_SESSION_CLIENT" ]] || fail 'browse-session integration client is not executable'
[[ -x "$DEVICE_PROVISIONING_CLIENT" ]] || fail 'device-provisioning integration client is not executable'
[[ -x "$REAL_BTRFS_TESTS" ]] || fail 'real-Btrfs C++ integration test is not executable'
[[ -x "$REAL_INSTALLED_RUNTIME_TESTS" ]] || fail 'installed-runtime C++ integration test is not executable'
[[ -x "$REAL_MAPPER_LIFECYCLE_TESTS" ]] || fail 'mapper-lifecycle C++ integration test is not executable'
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
run_first_backup_through_system_dbus
grep -q '"incremental": false' "$RUN_LOG" || fail 'full stream was not used for first backup'
grep -q '^profile_id=default$' /var/lib/btrfs-backup/profiles/default/last-success \
    || fail 'profile last-success state was not written'
grep -q '"state": "succeeded"' /var/lib/btrfs-backup/history/default/last.json \
    || fail 'history JSON was not written'
set +e
CTL_STATUS_OUTPUT="$(btrfs-backupctl status show --profile default --human 2>&1)"
CTL_STATUS_CODE=$?
set -e
[[ "$CTL_STATUS_CODE" -eq 0 ]] \
    || { printf '%s\n' "$CTL_STATUS_OUTPUT" >&2; fail 'btrfs-backupctl status failed'; }
grep -q '^Default backup: succeeded$' <<< "$CTL_STATUS_OUTPUT" \
    || { printf '%s\n' "$CTL_STATUS_OUTPUT" >&2; fail 'btrfs-backupctl did not render human status'; }
set +e
CTL_HISTORY_OUTPUT="$(btrfs-backupctl status history --profile default --limit 1 2>&1)"
CTL_HISTORY_CODE=$?
set -e
[[ "$CTL_HISTORY_CODE" -eq 0 ]] \
    || { printf '%s\n' "$CTL_HISTORY_OUTPUT" >&2; fail 'btrfs-backupctl history failed'; }
grep -q '"state": "succeeded"' <<< "$CTL_HISTORY_OUTPUT" \
    || { printf '%s\n' "$CTL_HISTORY_OUTPUT" >&2; fail 'btrfs-backupctl did not render history'; }
assert_count 1 "$TARGET_MOUNT/snapshots/home"
assert_count 1 "$SOURCE_MOUNT/.snapshots/home"
assert_remote_matches_latest_local
pass 'system D-Bus backup transfers and verifies real Btrfs data'

manager_independence_test
trusted_hook_security_test
"$REAL_MAPPER_LIFECYCLE_TESTS" "$TARGET_MOUNT" "$TARGET_LOOP" "$MAPPER_NAME" "$PASSPHRASE_FILE"
sandboxed_systemd_service_test
sandboxed_auto_eject_test

printf 'Real Btrfs integration test completed in %s\n' "$TEST_ROOT"
