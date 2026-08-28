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
SOURCE_MOUNT=/mnt/bb-real-source
TARGET_MOUNT=/mnt/btrfs-backup/default
TARGET_STAGING_MOUNT=/mnt/bb-real-target-staging
MAPPER_NAME=bb-real-target
MAPPER_PATH="/dev/mapper/$MAPPER_NAME"
PASSPHRASE_FILE="$TEST_ROOT/luks.pass"
PACKAGE_DIR="$TEST_ROOT/package"
RENDERED_CONFIG="$TEST_ROOT/rendered"
LOG_DIR="$TEST_ROOT/logs"
RUN_LOG="$LOG_DIR/btrfs-backup.log"
PROFILE_JSON=/etc/btrfs-backup/profiles/default/profile.json
EJECT_COMPLETION_COUNT=0

cleanup() {
    set +e
    umount -R "$TARGET_MOUNT" 2>/dev/null
    umount -R "$TARGET_STAGING_MOUNT" 2>/dev/null
    cryptsetup close "$MAPPER_NAME" 2>/dev/null
    umount -R "$SOURCE_MOUNT" 2>/dev/null
    [[ -n "$TARGET_LOOP" ]] && losetup -d "$TARGET_LOOP" 2>/dev/null
    [[ -n "$SOURCE_LOOP" ]] && losetup -d "$SOURCE_LOOP" 2>/dev/null
    rm -rf -- "$TEST_ROOT" "$SOURCE_MOUNT" "$TARGET_MOUNT" "$TARGET_STAGING_MOUNT"
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

build_and_verify_packages() {
    local base_packages=()
    local kde_packages=()
    local base_metadata

    "$ROOT/tools/build-release.sh" --target arch --skip-tests --dist-dir "$PACKAGE_DIR" >/dev/null
    base_packages=("$PACKAGE_DIR"/btrfs-backup-[0-9]*.pkg.tar.zst)
    kde_packages=("$PACKAGE_DIR"/btrfs-backup-kde-*.pkg.tar.zst)
    (( ${#base_packages[@]} == 1 )) || fail "expected one base package, found ${#base_packages[@]}"
    (( ${#kde_packages[@]} == 1 )) || fail "expected one KDE package, found ${#kde_packages[@]}"

    base_metadata="$(tar --zstd -xOf "${base_packages[0]}" .PKGINFO)"
    if grep -Eq '^depend = (extra-cmake-modules|ki18n|kirigami|kpackage|kservice|libplasma|qt6-[^ <>=]+)' \
        <<< "$base_metadata"; then
        fail 'base package has a KDE or Qt runtime dependency'
    fi

    pacman -U --noconfirm "${base_packages[0]}" >/dev/null
    if pacman -Q btrfs-backup-kde >/dev/null 2>&1; then
        fail 'KDE package was installed with the base package'
    fi
    command -v btrfs-backup >/dev/null
    command -v btrfs-backupctl >/dev/null
    command -v btrfs-backupd >/dev/null
    btrfs-backup --help >/dev/null
    btrfs-backupctl --help >/dev/null
    btrfs-backupd --help >/dev/null
    if ldd /usr/bin/btrfs-backup /usr/bin/btrfs-backupctl /usr/bin/btrfs-backupd \
        | grep -Eq 'lib(Qt6|KF6|Plasma)'; then
        fail 'base commands link to a KDE or Qt runtime library'
    fi
    pass 'base package installs and runs without the KDE package'

    pacman -U --noconfirm "${kde_packages[0]}" >/dev/null
    pacman -Q btrfs-backup-kde >/dev/null
    pass 'KDE package installs separately from the base package'

    pacman -R --noconfirm btrfs-backup-kde >/dev/null
    if pacman -Q btrfs-backup-kde >/dev/null 2>&1; then
        fail 'KDE package remained installed after removal'
    fi
}

configure_backup_with_cli() {
    local target_device="$1"
    local luks_uuid="$2"
    local btrfs_uuid="$3"

    install -d -m0750 "$RENDERED_CONFIG/config" "$RENDERED_CONFIG/systemd" "$RENDERED_CONFIG/udev"
    btrfs-backupctl profile create \
        --output "$RENDERED_CONFIG/config/profile.json" \
        --profile default \
        --name 'Default backup' \
        --device "$target_device" \
        --luks-uuid "$luks_uuid" \
        --btrfs-uuid "$btrfs_uuid" \
        --mapper-name "$MAPPER_NAME" \
        --remote-retention 2 \
        --local-retention 2 \
        --daily-limit false \
        --incremental-required true \
        --keep-failed-local-snapshot false \
        --auto-eject false \
        --minimum-target-free-bytes 0 \
        --minimum-local-free-bytes 0 \
        --source home home "$SOURCE_MOUNT/home" "$SOURCE_MOUNT/.snapshots/home" home 2 2 >/dev/null
    btrfs-backupctl \
        profile \
        --etc-root "$RENDERED_CONFIG/config" \
        --udev-root "$RENDERED_CONFIG/udev" \
        --systemd-root "$RENDERED_CONFIG/systemd" \
        --public-root "$RENDERED_CONFIG/public/profiles" \
        save --file "$RENDERED_CONFIG/config/profile.json" >/dev/null
    btrfs-backupctl installation render \
        --file "$RENDERED_CONFIG/config/profile.json" \
        --output-dir "$RENDERED_CONFIG" \
        --backup-command '/usr/bin/btrfs-backupctl runner execute' \
        --eject-script '/usr/bin/btrfs-backupctl target eject' \
        --keyfile none
    btrfs-backupctl installation validate --rendered-root "$RENDERED_CONFIG" >/dev/null

    install -d -m0700 /etc/btrfs-backup /etc/btrfs-backup/profiles/default
    install -m0600 "$RENDERED_CONFIG/config/profile.json" /etc/btrfs-backup/profiles/default/profile.json
    install -Dm0644 "$RENDERED_CONFIG/systemd/btrfs-backup.service" /etc/systemd/system/btrfs-backup.service
    install -Dm0644 "$RENDERED_CONFIG/systemd/btrfs-backup@.service" /etc/systemd/system/btrfs-backup@.service
    install -Dm0644 "$RENDERED_CONFIG/systemd/btrfs-backup-eject@.service" /etc/systemd/system/btrfs-backup-eject@.service
    install -Dm0644 \
        "$RENDERED_CONFIG/systemd/btrfs-backup@default.service.d/target-mount.conf" \
        /etc/systemd/system/btrfs-backup@default.service.d/target-mount.conf
    install -Dm0644 "$RENDERED_CONFIG/udev/99-btrfs-backup-default.rules" /etc/udev/rules.d/99-btrfs-backup-default.rules
    systemctl daemon-reload
    btrfs-backupctl installation validate --active --profile default >/dev/null
    PROFILE_JSON=/etc/btrfs-backup/profiles/default/profile.json
    [[ -f "$PROFILE_JSON" ]] || fail 'configuration did not create default profile JSON'
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

write_pending_marker() {
    local local_snapshot="$1"
    local final_snapshot="$2"
    local run_id="$3"
    local state_dir=/var/lib/btrfs-backup/profiles/default
    local marker="$state_dir/pending-home"

    install -d -m0700 "$state_dir"
    {
        printf 'source_name=home\n'
        printf 'local_snapshot_path=%s\n' "$local_snapshot"
        printf 'final_snapshot_path=%s\n' "$final_snapshot"
        printf 'run_id=%s\n' "$run_id"
        printf 'timestamp=2026-08-24T12:00:00Z\n'
    } > "$marker"
    chmod 0600 "$marker"
    sync
}

recover_interrupted_before_receive() {
    local interrupted="$SOURCE_MOUNT/.snapshots/home/home-2026-08-20T000000Z"
    local final="$TARGET_MOUNT/snapshots/home/$(basename -- "$interrupted")"
    local marker=/var/lib/btrfs-backup/profiles/default/pending-home

    btrfs subvolume snapshot -r "$SOURCE_MOUNT/home" "$interrupted" >/dev/null
    write_pending_marker "$interrupted" "$final" '20260820T000000Z-interrupted'
    run_backup

    [[ ! -e "$interrupted" ]] || fail 'orphaned local snapshot survived pending recovery'
    [[ ! -e "$marker" ]] || fail 'pending marker survived pre-receive recovery'
    assert_no_incoming_children
    pass 'pending recovery removes an orphan left before receive'
}

recover_interrupted_after_commit() {
    local latest_local latest_remote marker

    latest_local="$(find "$SOURCE_MOUNT/.snapshots/home" -mindepth 1 -maxdepth 1 -type d -name 'home-*' | sort | tail -n1)"
    latest_remote="$(find "$TARGET_MOUNT/snapshots/home" -mindepth 1 -maxdepth 1 -type d -name 'home-*' | sort | tail -n1)"
    marker=/var/lib/btrfs-backup/profiles/default/pending-home
    [[ -n "$latest_local" && -n "$latest_remote" ]] || fail 'committed snapshot recovery setup is incomplete'

    write_pending_marker "$latest_local" "$latest_remote" '20260824T120000Z-committed'
    run_backup

    [[ ! -e "$marker" ]] || fail 'pending marker survived committed snapshot recovery'
    [[ -d "$latest_local" ]] || fail 'committed local snapshot was not preserved during recovery'
    [[ -d "$latest_remote" ]] || fail 'committed remote snapshot was not preserved during recovery'
    assert_remote_matches_latest_local
    assert_no_incoming_children
    pass 'pending recovery preserves a snapshot committed before interruption'
}

restore_latest_snapshot() {
    local latest_remote restore_root restored

    latest_remote="$(find "$TARGET_MOUNT/snapshots/home" -mindepth 1 -maxdepth 1 -type d -name 'home-*' | sort | tail -n1)"
    restore_root="$SOURCE_MOUNT/restore-drill"
    restored="$restore_root/$(basename -- "$latest_remote")"
    [[ -n "$latest_remote" ]] || fail 'restore drill has no remote snapshot'

    install -d -m0700 "$restore_root"
    btrfs send "$latest_remote" | btrfs receive "$restore_root" >/dev/null
    if ! diff -qr "$latest_remote" "$restored" >/dev/null; then
        diff -qr "$latest_remote" "$restored" >&2 || true
        fail 'restored snapshot content differs from the repository snapshot'
    fi
    btrfs subvolume delete -- "$restored" >/dev/null
    rmdir -- "$restore_root"
    pass 'latest repository snapshot completes a full restore drill'
}

manager_independence_test() {
    local hook_dir=/etc/btrfs-backup/hooks.d
    local hook="$hook_dir/manager-independence-test"
    local marker="$TEST_ROOT/manager-independence.marker"
    local profile_backup="$TEST_ROOT/profile.manager-independence.json.bak"
    local runner_log="$LOG_DIR/manager-independence.log"
    local runner_pid

    cp -a -- "$PROFILE_JSON" "$profile_backup"
    perl -0pi -e \
        's#"beforeSnapshot": \[\]#"beforeSnapshot": [{"type":"program","program":"/etc/btrfs-backup/hooks.d/manager-independence-test","arguments":[],"timeoutSeconds":30}]#' \
        "$PROFILE_JSON"
    chmod 0600 "$PROFILE_JSON"
    {
        printf '#!/bin/sh\n'
        printf "printf 'started\\n' > '%s'\n" "$marker"
        printf '/usr/bin/sleep 5\n'
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
    rm -f -- "$profile_backup" "$hook" "$marker"
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

systemd_security_audit() {
    local security_log="$LOG_DIR/systemd-security.log"

    if ! "$ROOT/tests/systemd/check-security.sh" \
        /etc/systemd/system/btrfs-backup@.service > "$security_log" 2>&1; then
        cat -- "$security_log" >&2
        fail 'systemd security exposure exceeds the accepted threshold'
    fi
    grep -Fq 'Overall exposure level' "$security_log" \
        || fail 'systemd security audit did not report an exposure level'
    pass 'systemd security audit accepts the installed profile service'
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
    fail 'timed out waiting for the target eject service'
}

sandboxed_systemd_service_test() {
    local mount_unit
    local mount_unit_path
    mount_unit="$(systemd-escape -p --suffix=mount "$TARGET_MOUNT")"
    mount_unit_path="/etc/systemd/system/$mount_unit"
    printf 'systemd sandbox\n' >> "$SOURCE_MOUNT/home/file-a.txt"
    sync
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
    sync
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
    [[ ! -e "$MAPPER_PATH" ]] || fail 'LUKS mapper remained open after the eject service'
    pass 'automatic eject runs outside the backup mount namespace'
}

validate_runtime_preflight() {
    INVOCATION_ID=real-docker-test btrfs-backup --validate --no-eject >/dev/null
}

target_uuid_mismatch_test() {
    perl -0pi -e 's#"btrfsUuid": "[^"]*"#"btrfsUuid": "00000000-0000-0000-0000-000000000000"#' "$PROFILE_JSON"
    expect_backup_failure 'Btrfs UUID mismatch'
    pass 'runtime rejects a mismatched target Btrfs UUID'
}

source_on_target_test() {
    local profile_backup="$TEST_ROOT/profile.source-on-target.json.bak"
    btrfs subvolume create "$TARGET_MOUNT/bad-source" >/dev/null
    install -d -m0700 "$TARGET_MOUNT/.bad-local"
    cp -a -- "$PROFILE_JSON" "$profile_backup"
    perl -0pi -e 's#"subvolume": "[^"]*"#"subvolume": "'"$TARGET_MOUNT"'/bad-source"#' "$PROFILE_JSON"
    perl -0pi -e 's#"localSnapshotDir": "[^"]*"#"localSnapshotDir": "'"$TARGET_MOUNT"'/.bad-local"#' "$PROFILE_JSON"
    chmod 0600 "$PROFILE_JSON"
    expect_backup_failure 'SOURCE_SUBVOLUME must not be on the backup target filesystem'
    cp -a -- "$profile_backup" "$PROFILE_JSON"
    rm -f -- "$profile_backup"
    btrfs subvolume delete -- "$TARGET_MOUNT/bad-source" >/dev/null
    rm -rf -- "$TARGET_MOUNT/.bad-local"
    pass 'runtime rejects a source on the backup target filesystem'
}

incoming_symlink_escape_test() {
    local outside="$TEST_ROOT/incoming-escape-target"
    local link="$TARGET_MOUNT/.incoming/home"

    install -d -m0700 "$outside"
    printf 'keep\n' > "$outside/sentinel"
    ln -s -- "$outside" "$link"
    expect_backup_failure 'Too many levels of symbolic links'
    [[ "$(cat -- "$outside/sentinel")" == 'keep' ]] \
        || fail 'incoming cleanup modified data outside the target repository'
    rm -f -- "$link"
    pass 'runtime rejects an incoming symlink escape without touching its target'
}

missing_incremental_parent_test() {
    local empty_local_dir="$SOURCE_MOUNT/.snapshots/empty-parent-check"
    local profile_backup="$TEST_ROOT/profile.parent-check.json.bak"

    cp -a -- "$PROFILE_JSON" "$profile_backup"
    install -d -m0700 "$empty_local_dir"
    perl -0pi -e 's#"localSnapshotDir": "[^"]*"#"localSnapshotDir": "'"$empty_local_dir"'"#' "$PROFILE_JSON"
    chmod 0600 "$PROFILE_JSON"
    printf 'delta\n' > "$SOURCE_MOUNT/home/orphan-parent-check.txt"
    sync
    expect_backup_failure 'Remote snapshots exist for home, but no UUID-matching local parent was found.'
    find "$empty_local_dir" -mindepth 1 -maxdepth 1 -type d -name 'home-*' -exec btrfs subvolume delete -- {} \; >/dev/null
    rmdir -- "$empty_local_dir"
    cp -a -- "$profile_backup" "$PROFILE_JSON"
    rm -f -- "$profile_backup"
    pass 'runtime rejects incremental backup when remote snapshots exist without a local parent'
}

require_root
require_commands btrfs cryptsetup dd diff dmsetup find findmnt grep ldd losetup mkfs.btrfs mknod mount pacman perl seq sha256sum stat systemd-escape tar tee truncate
ensure_loop_devices

install -d -m0755 "$SOURCE_MOUNT" "$TARGET_MOUNT"
install -d -m0700 "$LOG_DIR"
build_and_verify_packages

printf '%s\n' 'btrfs-backup-real-test-passphrase' > "$PASSPHRASE_FILE"
chmod 0600 "$PASSPHRASE_FILE"

truncate -s 768M "$SOURCE_IMAGE"
truncate -s 768M "$TARGET_IMAGE"
SOURCE_LOOP="$(losetup --find --show "$SOURCE_IMAGE")"
TARGET_LOOP="$(losetup --find --show "$TARGET_IMAGE")"

mkfs.btrfs -q -f "$SOURCE_LOOP"
cryptsetup luksFormat --batch-mode --type luks2 --key-file "$PASSPHRASE_FILE" "$TARGET_LOOP"
cryptsetup open --key-file "$PASSPHRASE_FILE" "$TARGET_LOOP" "$MAPPER_NAME"
udevadm settle --timeout=10
dmsetup mknodes "$MAPPER_NAME"
[[ -b "$MAPPER_PATH" ]] || fail "cryptsetup mapper was not created: $MAPPER_PATH"
mkfs.btrfs -q -f "$MAPPER_PATH"

mount -o noatime,compress=zstd:3 "$SOURCE_LOOP" "$SOURCE_MOUNT"
btrfs subvolume create "$SOURCE_MOUNT/home" >/dev/null
install -d -m0700 "$SOURCE_MOUNT/.snapshots/home"

mount -o noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd:3 "$MAPPER_PATH" "$TARGET_MOUNT"
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
btrfs-backupctl target mount --profile default >/dev/null
pass 'installed mount command validates the mounted target'
with_restored_file "$PROFILE_JSON" target_uuid_mismatch_test
source_on_target_test
incoming_symlink_escape_test

run_backup
grep -q '"incremental": false' "$RUN_LOG" || fail 'full stream was not used for first backup'
grep -q '^profile_id=default$' /var/lib/btrfs-backup/profiles/default/last-success \
    || fail 'profile last-success state was not written'
grep -q '"state": "succeeded"' /run/btrfs-backup/profiles/default/current.json \
    || fail 'current status JSON was not written'
grep -q '"state": "succeeded"' /var/lib/btrfs-backup/history/default/last.json \
    || fail 'history JSON was not written'
set +e
CTL_STATUS_OUTPUT="$(btrfs-backupctl status show --profile default --human 2>&1)"
CTL_STATUS_CODE=$?
set -e
[[ "$CTL_STATUS_CODE" -eq 0 ]] \
    || { printf '%s\n' "$CTL_STATUS_OUTPUT" >&2; fail 'btrfs-backupctl status failed'; }
grep -q '^default: succeeded$' <<< "$CTL_STATUS_OUTPUT" \
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
pass 'full backup transfers and verifies real Btrfs data'

printf 'beta\n' >> "$SOURCE_MOUNT/home/file-a.txt"
rm -f -- "$SOURCE_MOUNT/home/dir/blob.bin"
dd if=/dev/urandom of="$SOURCE_MOUNT/home/dir/blob-2.bin" bs=1M count=12 status=none
sync
run_backup
grep -q '"incremental": true' "$RUN_LOG" || fail 'incremental stream was not used for second backup'
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

recover_interrupted_before_receive
recover_interrupted_after_commit
restore_latest_snapshot
manager_independence_test
trusted_hook_security_test
systemd_security_audit
sandboxed_systemd_service_test
sandboxed_auto_eject_test

printf 'Real Btrfs integration test completed in %s\n' "$TEST_ROOT"
