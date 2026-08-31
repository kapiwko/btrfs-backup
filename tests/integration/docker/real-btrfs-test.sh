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
MAPPER_NAME="bb-real-target-${TEST_ROOT##*.}"
MAPPER_PATH="/dev/mapper/$MAPPER_NAME"
PASSPHRASE_FILE="$TEST_ROOT/luks.pass"
PACKAGE_DIR="${BTRFSBACKUP_PACKAGE_DIR:-$TEST_ROOT/package}"
RENDERED_CONFIG="$TEST_ROOT/rendered"
LOG_DIR="$TEST_ROOT/logs"
RUN_LOG="$LOG_DIR/btrfs-backup.log"
PROFILE_JSON=/etc/btrfs-backup/profiles/default/profile.json
EJECT_COMPLETION_COUNT=0

cleanup() {
    set +e
    umount -R "$TARGET_MOUNT" 2>/dev/null
    umount -R "$TARGET_STAGING_MOUNT" 2>/dev/null
    for _ in $(seq 1 5); do
        timeout --kill-after=1s 1s cryptsetup close "$MAPPER_NAME" 2>/dev/null && break
        sleep 0.1
    done
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
    local base_metadata

    if ! compgen -G "$PACKAGE_DIR/btrfs-backup-[0-9]*.pkg.tar.zst" >/dev/null; then
        "$ROOT/tools/build-release.sh" --target arch-base --skip-tests --dist-dir "$PACKAGE_DIR" >/dev/null
    fi
    base_packages=("$PACKAGE_DIR"/btrfs-backup-[0-9]*.pkg.tar.zst)
    (( ${#base_packages[@]} == 1 )) || fail "expected one base package, found ${#base_packages[@]}"

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
    command -v pkaction >/dev/null \
        || fail 'base package did not install its polkit runtime dependency'
    btrfs-backup --help >/dev/null
    btrfs-backupctl --help >/dev/null
    btrfs-backupd --help >/dev/null
    if ldd /usr/bin/btrfs-backup /usr/bin/btrfs-backupctl /usr/bin/btrfs-backupd \
        | grep -Eq 'lib(Qt6|KF6|Plasma)'; then
        fail 'base commands link to a KDE or Qt runtime library'
    fi
    pass 'base package installs and runs without KDE or Qt runtime dependencies'
}

configure_backup_with_cli() {
    local target_device="$1"
    local luks_uuid="$2"
    local btrfs_uuid="$3"
    local installed_keyfile=/etc/btrfs-backup/keys/default.key
    local mount_unit

    install -d -m0700 /etc/btrfs-backup/keys
    install -m0600 "$PASSPHRASE_FILE" "$installed_keyfile"

    install -d -m0750 "$RENDERED_CONFIG/config" "$RENDERED_CONFIG/systemd" "$RENDERED_CONFIG/udev"
    btrfs-backupctl profile create \
        --output "$RENDERED_CONFIG/config/profile.json" \
        --profile default \
        --name 'Default backup' \
        --device "$target_device" \
        --luks-uuid "$luks_uuid" \
        --btrfs-uuid "$btrfs_uuid" \
        --mapper-name "$MAPPER_NAME" \
        --keyfile "$installed_keyfile" \
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
        --eject-script '/usr/bin/btrfs-backupctl target eject'
    btrfs-backupctl installation validate --rendered-root "$RENDERED_CONFIG" >/dev/null

    install -d -m0700 /etc/btrfs-backup /etc/btrfs-backup/profiles/default
    install -m0600 "$RENDERED_CONFIG/config/profile.json" /etc/btrfs-backup/profiles/default/profile.json
    install -Dm0644 "$RENDERED_CONFIG/systemd/btrfs-backup.service" /etc/systemd/system/btrfs-backup.service
    install -Dm0644 "$RENDERED_CONFIG/systemd/btrfs-backup@.service" /etc/systemd/system/btrfs-backup@.service
    install -Dm0644 "$RENDERED_CONFIG/systemd/btrfs-backup-eject@.service" /etc/systemd/system/btrfs-backup-eject@.service
    install -Dm0644 "$RENDERED_CONFIG/systemd/btrfs-backup-validate@.service" /etc/systemd/system/btrfs-backup-validate@.service
    install -Dm0644 "$RENDERED_CONFIG/systemd/btrfs-backup-target@.service" /etc/systemd/system/btrfs-backup-target@.service
    mount_unit="$(systemd-escape -p --suffix=mount "$TARGET_MOUNT")"
    install -Dm0644 "$RENDERED_CONFIG/systemd/$mount_unit" "/etc/systemd/system/$mount_unit"
    install -Dm0644 \
        "$RENDERED_CONFIG/systemd/btrfs-backup@default.service.d/target-mount.conf" \
        /etc/systemd/system/btrfs-backup@default.service.d/target-mount.conf
    install -Dm0644 "$RENDERED_CONFIG/udev/99-btrfs-backup-default.rules" /etc/udev/rules.d/99-btrfs-backup-default.rules
    systemctl daemon-reload
    btrfs-backupctl installation validate --active --profile default >/dev/null
    PROFILE_JSON=/etc/btrfs-backup/profiles/default/profile.json
    [[ -f "$PROFILE_JSON" ]] || fail 'configuration did not create default profile JSON'
}

managed_target_lifecycle_test() {
    local mount_unit
    mount_unit="$(systemd-escape -p --suffix=mount "$TARGET_MOUNT")"
    umount "$TARGET_MOUNT"
    cryptsetup close "$MAPPER_NAME"
    [[ ! -e "$MAPPER_PATH" ]] || fail 'test mapper remained active before managed activation'

    systemctl start systemd-udevd.service
    systemctl is-active --quiet systemd-udevd.service \
        || fail 'systemd-udevd did not start before managed target activation'

    if ! systemctl start "$mount_unit"; then
        systemctl status --no-pager "$mount_unit" >&2 || true
        systemctl status --no-pager btrfs-backup-target@default.service >&2 || true
        cat "$PROFILE_JSON" >&2 || true
        systemctl list-units --all 'systemd-cryptsetup@*' >&2 || true
        ls -la /dev/mapper >&2 || true
        dmsetup ls --tree >&2 || true
        journalctl --no-pager -u "$mount_unit" -u btrfs-backup-target@default.service -n 100 >&2 || true
        fail 'native mount unit could not activate and mount the backup target'
    fi
    findmnt -n -M "$TARGET_MOUNT" >/dev/null \
        || fail 'native mount unit did not mount the backup target'
    [[ -b "$MAPPER_PATH" ]] \
        || fail 'managed target service did not activate the LUKS mapper'
    [[ -f /run/btrfs-backup/target-activation/default.json ]] \
        || fail 'managed target activation did not record mapper ownership'
    pass 'native mount unit activates LUKS without fstab or crypttab'
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

real_browse_session_test() {
    local test_user=btrfs-browse-test
    local policy_rule=/etc/polkit-1/rules.d/49-btrfs-backup-browse-integration.rules
    local client="/tmp/btrfs-backup-browse-session-client.$$"
    local hold="/tmp/btrfs-backup-browse-session-hold.$$"
    local response="$TEST_ROOT/browse-session.json"
    local errors="$TEST_ROOT/browse-session.err"
    local browse_root options

    cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
        "$ROOT/tests/integration/BrowseSessionClient.c" -lsystemd -o "$client"
    useradd --system --no-create-home --shell /usr/bin/nologin "$test_user"
    cat > "$policy_rule" <<'EOF_POLKIT_RULE'
polkit.addRule(function(action, subject) {
    if (action.id == "io.github.btrfsbackup.open-browse-session" &&
        subject.user == "btrfs-browse-test") {
        return polkit.Result.YES;
    }
});
EOF_POLKIT_RULE
    chmod 0644 "$policy_rule"
    : > "$hold"
    chmod 0644 "$hold"
    systemctl start polkit.service btrfs-backupd.service

    runuser -u "$test_user" -- "$client" default "$hold" > "$response" 2> "$errors" &
    local client_pid=$!
    for _ in $(seq 1 200); do
        [[ -s "$response" ]] && break
        if ! kill -0 "$client_pid" 2>/dev/null; then
            cat "$errors" >&2 || true
            journalctl --no-pager -u btrfs-backupd.service -n 100 >&2 || true
            fail 'browse session client exited before opening a session'
        fi
        sleep 0.05
    done
    [[ -s "$response" ]] || fail 'browse session client did not return a session'
    browse_root="$(sed -n 's/.*"rootPath":[[:space:]]*"\([^"]*\)".*/\1/p' "$response")"
    [[ "$browse_root" == /run/btrfs-backup-browse/*/repository ]] \
        || fail "manager returned an invalid browse root: $browse_root"
    options="$(findmnt -n -o OPTIONS -M "$browse_root")"
    for option in ro nodev nosuid noexec nosymfollow; do
        grep -Eq "(^|,)$option(,|$)" <<< "$options" \
            || fail "browse mount is missing $option: $options"
    done
    cmp "$TARGET_MOUNT/snapshots/browse-probe.txt" "$browse_root/browse-probe.txt" \
        || fail 'browse session did not expose repository data'

    rm -f -- "$hold"
    wait "$client_pid"
    for _ in $(seq 1 200); do
        [[ ! -e "${browse_root%/repository}" ]] && break
        sleep 0.05
    done
    [[ ! -e "${browse_root%/repository}" ]] \
        || fail 'browse session survived D-Bus caller disconnect'
    findmnt -n -M "$TARGET_MOUNT" >/dev/null \
        || fail 'browse cleanup unmounted a target it did not mount'

    systemctl stop btrfs-backupd.service polkit.service
    rm -f -- "$client" "$policy_rule"
    userdel "$test_user"
    pass 'real browse session is read-only and cleans up after caller disconnect'
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
    sync -f "$marker"
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

restore_engine_test() {
    local repository="$TARGET_MOUNT/snapshots"
    local latest_remote relative snapshot_uuid received_uuid created_at
    local restored="$SOURCE_MOUNT/restore-engine-result"
    local drill_destination="$SOURCE_MOUNT/restore-engine-drill/result"

    latest_remote="$(find "$repository/home" -mindepth 1 -maxdepth 1 -type d -name 'home-*' | sort | tail -n1)"
    [[ -n "$latest_remote" ]] || fail 'restore engine has no repository snapshot'
    relative="home/$(basename -- "$latest_remote")"
    snapshot_uuid="$(btrfs subvolume show "$latest_remote" | sed -n 's/^[[:space:]]*UUID:[[:space:]]*//p' | head -n1)"
    received_uuid="$(btrfs subvolume show "$latest_remote" | sed -n 's/^[[:space:]]*Received UUID:[[:space:]]*//p' | head -n1)"
    created_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    [[ -n "$snapshot_uuid" && -n "$received_uuid" ]] \
        || fail 'restore engine could not read real Btrfs snapshot identity'

    cat > "$repository/repository.json" <<EOF_REPOSITORY
{"schemaVersion":1,"repositoryId":"real-$TARGET_BTRFS_UUID","targetFilesystemUuid":"$TARGET_BTRFS_UUID","createdAt":"$created_at","features":["catalog-v1"]}
EOF_REPOSITORY
    cat > "$repository/catalog.json" <<EOF_CATALOG
{"schemaVersion":1,"generation":1,"snapshots":[{"snapshotId":"real-latest","hostId":"real-host","profileId":"default","sourceId":"home","relativePath":"$relative","createdAt":"$created_at","uuid":"$snapshot_uuid","receivedUuid":"$received_uuid","verified":true}]}
EOF_CATALOG

    btrfs-backupctl restore plan \
        --repository "$repository" \
        --snapshot real-latest \
        --source . \
        --destination "$restored" \
        --transaction real-plan \
        --subvolume >/dev/null
    [[ ! -e "$restored" ]] || fail 'restore plan mutated its destination'
    btrfs-backupctl restore execute \
        --repository "$repository" \
        --snapshot real-latest \
        --source . \
        --destination "$restored" \
        --transaction real-execute \
        --subvolume >/dev/null
    btrfs subvolume show "$restored" >/dev/null \
        || fail 'restore engine did not create a Btrfs subvolume'
    if ! diff -qr "$latest_remote" "$restored" >/dev/null; then
        diff -qr "$latest_remote" "$restored" >&2 || true
        fail 'restore engine output differs from the selected repository snapshot'
    fi

    btrfs-backupctl restore drill \
        --repository "$repository" \
        --snapshot real-latest \
        --source . \
        --destination "$drill_destination" \
        --transaction real-drill >/dev/null
    [[ ! -e "$drill_destination" ]] || fail 'restore drill published a destination'
    [[ ! -e "$SOURCE_MOUNT/restore-engine-drill/.btrfs-backup-restore-real-drill.staging" ]] \
        || fail 'restore drill left staging data'

    btrfs subvolume delete -- "$restored" >/dev/null
    rmdir -- "$SOURCE_MOUNT/restore-engine-drill"
    pass 'restore engine plans, restores and drills against real Btrfs snapshots'
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

plain_mapper_lifecycle_test() {
    umount "$TARGET_MOUNT"
    cryptsetup close "$MAPPER_NAME"
    [[ ! -e "$MAPPER_PATH" ]] || fail 'plain unmount did not close the test mapper'
    cryptsetup open --key-file "$PASSPHRASE_FILE" "$TARGET_LOOP" "$MAPPER_NAME"
    udevadm settle --timeout=10
    dmsetup mknodes "$MAPPER_NAME"
    mount -o noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd:3 \
        "$MAPPER_PATH" "$TARGET_MOUNT"
    pass 'plain unmount closes and reopens the test mapper'
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
require_commands btrfs busctl cc cmp cryptsetup date dd diff dmsetup find findmnt grep journalctl ldd losetup mkfifo mkfs.btrfs mknod mount pacman perl runuser seq sha256sum stat systemd-escape tar tee timeout truncate useradd userdel
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
install -d -m0700 "$SOURCE_MOUNT/.snapshots/home"

mount -o noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd:3 "$MAPPER_PATH" "$TARGET_MOUNT"
install -d -m0700 "$TARGET_MOUNT/snapshots" "$TARGET_MOUNT/.incoming"

printf 'alpha\n' > "$SOURCE_MOUNT/home/file-a.txt"
install -d -m0755 "$SOURCE_MOUNT/home/dir"
dd if=/dev/urandom of="$SOURCE_MOUNT/home/dir/blob.bin" bs=1M count=2 status=none
sync -f "$SOURCE_MOUNT"

plain_mapper_lifecycle_test

TARGET_LUKS_UUID="$(cryptsetup luksUUID "$TARGET_LOOP")"
TARGET_BTRFS_UUID="$(findmnt -n -o UUID -M "$TARGET_MOUNT")"
configure_backup_with_cli "$TARGET_LOOP" "$TARGET_LUKS_UUID" "$TARGET_BTRFS_UUID"
pass 'installed CLI renders, installs, and validates configuration'
printf 'browse probe\n' > "$TARGET_MOUNT/snapshots/browse-probe.txt"
real_browse_session_test
rm -f -- "$TARGET_MOUNT/snapshots/browse-probe.txt"
managed_target_lifecycle_test
validate_runtime_preflight
pass 'installed runtime validates the mounted target'
btrfs-backupctl target mount --profile default >/dev/null
pass 'installed mount command validates the mounted target'
with_restored_file "$PROFILE_JSON" target_uuid_mismatch_test
source_on_target_test
incoming_symlink_escape_test

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
pass 'full backup transfers and verifies real Btrfs data'

printf 'beta\n' >> "$SOURCE_MOUNT/home/file-a.txt"
rm -f -- "$SOURCE_MOUNT/home/dir/blob.bin"
dd if=/dev/urandom of="$SOURCE_MOUNT/home/dir/blob-2.bin" bs=1M count=3 status=none
sync -f "$SOURCE_MOUNT"
run_backup
grep -q '"incremental": true' "$RUN_LOG" || fail 'incremental stream was not used for second backup'
assert_count 2 "$TARGET_MOUNT/snapshots/home"
assert_count 2 "$SOURCE_MOUNT/.snapshots/home"
assert_remote_matches_latest_local
pass 'incremental backup transfers and verifies real Btrfs data'
missing_incremental_parent_test

printf 'gamma\n' > "$SOURCE_MOUNT/home/new-file.txt"
dd if=/dev/urandom of="$SOURCE_MOUNT/home/dir/blob-3.bin" bs=1M count=1 status=none
sync -f "$SOURCE_MOUNT"
run_backup
assert_count 2 "$TARGET_MOUNT/snapshots/home"
assert_count 2 "$SOURCE_MOUNT/.snapshots/home"
assert_remote_matches_latest_local
assert_no_incoming_children
pass 'retention keeps the latest two local and remote snapshots'

recover_interrupted_before_receive
recover_interrupted_after_commit
restore_latest_snapshot
restore_engine_test
manager_independence_test
trusted_hook_security_test
systemd_security_audit
plain_mapper_lifecycle_test
sandboxed_systemd_service_test
sandboxed_auto_eject_test

printf 'Real Btrfs integration test completed in %s\n' "$TEST_ROOT"
