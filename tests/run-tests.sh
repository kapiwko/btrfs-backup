#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C.UTF-8

MODE=full
case "${1:-}" in
    "") ;;
    --full) MODE=full ;;
    --static-only) MODE=static ;;
    -h|--help)
        cat <<'USAGE'
Usage: tests/run-tests.sh [--full|--static-only]

  --full         Run syntax, render, profile JSON and status CLI tests (default).
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
TESTS_RUN=0
CTEST_JOBS="${CTEST_JOBS:-${BUILD_JOBS:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}}"

cleanup() {
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
    local ctest_log="$TEST_ROOT/ctest.log"

    make -C "$ROOT" >/dev/null
    if ! ctest \
        --test-dir "$ROOT/build" \
        --parallel "$CTEST_JOBS" \
        --output-on-failure >"$ctest_log" 2>&1; then
        cat "$ctest_log" >&2
        fail 'CTest suite failed'
    fi
    mapfile -t scripts < <(find "$ROOT" -type f \( -name '*.sh' -o -name '*.install' \) | sort)
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
    "$ROOT/build/btrfs-backupctl" profile create \
        --output "$profile" \
        --profile laptop \
        --name 'Laptop backup' \
        --device /dev/disk/by-uuid/11111111-2222-3333-4444-555555555555 \
        --luks-uuid 11111111-2222-3333-4444-555555555555 \
        --btrfs-uuid 66666666-7777-8888-9999-aaaaaaaaaaaa \
        --partition-uuid aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee \
        --mapper-name backupdisk \
        --remote-retention 30 \
        --local-retention 30 \
        --minimum-target-free-bytes 5368709120 \
        --minimum-local-free-bytes 1073741824 \
        --source root root / /.snapshots/btrfs-backup/root root 30 30 \
        --source home home /home /.snapshots/btrfs-backup/home home 45 20 >/dev/null
    "$ROOT/build/btrfs-backupctl" \
        profile \
        --etc-root "$output/config" \
        --udev-root "$output/udev" \
        --systemd-root "$output/systemd" \
        --public-root "$output/public/profiles" \
        save --file "$profile" >/dev/null
    "$ROOT/build/btrfs-backupctl" installation render \
        --file "$profile" \
        --output-dir "$output" \
        --backup-command "$ROOT/build/btrfs-backupctl runner execute" \
        --eject-script "$ROOT/build/btrfs-backupctl target eject"
    "$ROOT/build/btrfs-backupctl" installation validate --rendered-root "$output" >/dev/null

    assert_file "$output/config/profile.json"
    assert_file "$output/config/profiles/laptop/profile.json"
    assert_contains "$output/config/profile.json" '"profileId": "laptop"'
    assert_file "$output/systemd/btrfs-backup.service"
    assert_file "$output/systemd/btrfs-backup@.service"
    assert_file "$output/systemd/btrfs-backup-eject@.service"
    assert_file "$output/systemd/btrfs-backup-target@.service"
    assert_file "$output/systemd/mnt-btrfs\\x2dbackup-laptop.mount"
    assert_file "$output/systemd/btrfs-backup@laptop.service.d/target-mount.conf"
    assert_file "$output/udev/99-btrfs-backup-laptop.rules"
    assert_not_exists "$output/udev/99-btrfs-backup.rules"
    assert_not_contains "$output/udev/99-btrfs-backup-laptop.rules" 'ACTION=="remove"'
    assert_contains "$output/udev/99-btrfs-backup-laptop.rules" 'btrfs-backup@laptop.service'
    assert_not_contains "$output/systemd/btrfs-backup.service" 'WantedBy='
    assert_not_contains "$output/systemd/btrfs-backup.service" 'Requires=mnt-btrfs\x2dbackup-laptop.mount'
    assert_contains "$output/systemd/btrfs-backup.service" 'ExecStart='
    assert_contains "$output/systemd/btrfs-backup.service" '--profile laptop'
    assert_contains "$output/systemd/btrfs-backup.service" 'OnSuccess=btrfs-backup-eject@laptop.service'
    assert_contains "$output/systemd/btrfs-backup.service" 'OnFailure=btrfs-backup-eject@laptop.service'
    assert_contains "$output/systemd/btrfs-backup@.service" 'OnSuccess=btrfs-backup-eject@%i.service'
    assert_contains "$output/systemd/btrfs-backup@.service" 'OnFailure=btrfs-backup-eject@%i.service'
    assert_contains "$output/systemd/btrfs-backup-eject@.service" '--from-service --profile %i'
    assert_contains "$output/systemd/btrfs-backup.service" 'TimeoutStopSec=90s'
    assert_contains "$output/systemd/btrfs-backup.service" 'KillMode=mixed'
    assert_contains "$output/systemd/btrfs-backup.service" 'SendSIGKILL=yes'
    assert_contains "$output/systemd/btrfs-backup.service" 'NoNewPrivileges=yes'
    assert_contains "$output/systemd/btrfs-backup.service" 'PrivateTmp=yes'
    assert_contains "$output/systemd/btrfs-backup.service" 'ProtectSystem=full'
    assert_contains "$output/systemd/btrfs-backup.service" 'ProtectProc=invisible'
    assert_contains "$output/systemd/btrfs-backup.service" 'RestrictAddressFamilies=AF_UNIX AF_NETLINK'
    assert_contains "$output/systemd/btrfs-backup.service" 'Environment=PATH=/usr/bin'
    assert_contains "$output/systemd/btrfs-backup@laptop.service.d/target-mount.conf" 'RequiresMountsFor="/mnt/btrfs-backup/laptop"'
    assert_contains "$output/systemd/btrfs-backup.service" 'RequiresMountsFor="/mnt/btrfs-backup/laptop"'
    assert_contains "$output/systemd/mnt-btrfs\\x2dbackup-laptop.mount" 'Requires=btrfs-backup-target@laptop.service'
    assert_contains "$output/systemd/mnt-btrfs\\x2dbackup-laptop.mount" 'Options=noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd'
    assert_contains "$output/systemd/btrfs-backup-target@.service" 'target activate --from-service --profile %i'
    assert_not_exists "$output/config/fstab.fragment"
    assert_not_exists "$output/config/crypttab.fragment"
    if grep -R -q '{{' "$output"; then
        fail 'rendered output contains unresolved placeholders'
    fi
    pass 'backupctl renders validated multi-source configuration'
}

profile_json_test() {
    local rendered="$TEST_ROOT/profile-json-rendered"
    local saved="$TEST_ROOT/profile-json-saved"

    "$ROOT/build/btrfs-backupctl" profile validate --file "$ROOT/data/examples/profile.example.json" >/dev/null
    "$ROOT/build/btrfs-backupctl" profile render \
        --file "$ROOT/data/examples/profile.example.json" \
        --output-dir "$rendered" >/dev/null

    assert_not_exists "$rendered/etc/btrfs-backup/profiles.d/default.env"
    assert_file "$rendered/etc/btrfs-backup/profiles/default/profile.json"
    assert_file "$rendered/etc/udev/rules.d/99-btrfs-backup-default.rules"
    assert_file "$rendered/etc/systemd/system/btrfs-backup@default.service.d/target-mount.conf"
    assert_file "$rendered/var/lib/btrfs-backup/public/profiles/default.json"
    assert_contains "$rendered/etc/btrfs-backup/profiles/default/profile.json" '"id": "home"'
    assert_contains "$rendered/etc/udev/rules.d/99-btrfs-backup-default.rules" 'btrfs-backup@default.service'

    "$ROOT/build/btrfs-backupctl" profile \
        --etc-root "$saved/etc/btrfs-backup" \
        --udev-root "$saved/etc/udev/rules.d" \
        --systemd-root "$saved/etc/systemd/system" \
        --public-root "$saved/var/lib/btrfs-backup/public/profiles" \
        save --file "$ROOT/data/examples/profile.example.json" >/dev/null

    assert_not_exists "$saved/etc/btrfs-backup/profiles.d/default.env"
    assert_file "$saved/etc/btrfs-backup/profiles/default/profile.json"
    assert_file "$saved/etc/udev/rules.d/99-btrfs-backup-default.rules"
    assert_file "$saved/etc/systemd/system/btrfs-backup@default.service.d/target-mount.conf"
    assert_file "$saved/var/lib/btrfs-backup/public/profiles/default.json"
    "$ROOT/build/btrfs-backupctl" profile \
        --etc-root "$saved/etc/btrfs-backup" \
        show --profile default > "$saved/show.json"
    assert_contains "$saved/show.json" '"profileId": "default"'
    "$ROOT/build/btrfs-backupctl" profile \
        --etc-root "$saved/etc/btrfs-backup" \
        export --profile default --output "$saved/exported.json" >/dev/null
    assert_file "$saved/exported.json"
    assert_contains "$saved/exported.json" '"profileId": "default"'
    pass 'profile JSON validates, renders, and saves generated runtime files'
}

command_surface_test() {
    local output="$TEST_ROOT/command-surface"
    local ctl="$ROOT/build/btrfs-backupctl"

    install -d -m0750 "$output"
    "$ctl" --help > "$output/root.txt"
    "$ctl" profile --help > "$output/profile.txt"
    "$ctl" status --help > "$output/status.txt"

    assert_not_contains "$output/root.txt" 'state COMMAND'
    assert_not_contains "$output/profile.txt" 'sources --file'
    assert_not_contains "$output/status.txt" 'write [OPTIONS]'
    assert_not_contains "$output/status.txt" '--json'

    if "$ctl" state --help >/dev/null 2>&1; then
        fail 'removed state command is still accepted'
    fi
    if "$ctl" profile sources --help >/dev/null 2>&1; then
        fail 'removed profile sources command is still accepted'
    fi
    if "$ctl" profile migrate --help >/dev/null 2>&1; then
        fail 'unknown profile command with --help is accepted'
    fi
    if "$ctl" status write --help >/dev/null 2>&1; then
        fail 'removed status write command is still accepted'
    fi

    pass 'backupctl exposes only supported command groups'
}

release_notes_test() {
    local notes="$TEST_ROOT/release-notes.md"
    local initial_notes="$TEST_ROOT/initial-release-notes.md"

    "$ROOT/tools/render-release-notes.sh" 0.3.2 v0.3.1 > "$notes"
    assert_contains "$notes" "## What's New"
    assert_contains "$notes" '### Table-Free Target Management'
    assert_contains "$notes" '### Upgrade Notes'
    assert_contains "$notes" '### Plasma Integration'
    assert_contains "$notes" '## Artifacts'
    assert_contains "$notes" '/compare/v0.3.1...v0.3.2'
    assert_not_contains "$notes" '## Unreleased'
    assert_not_contains "$notes" '## 0.3.1'

    "$ROOT/tools/render-release-notes.sh" 0.1.0 > "$initial_notes"
    assert_contains "$initial_notes" '/tree/v0.1.0'
    assert_not_contains "$initial_notes" '/compare/'
    pass 'release notes render one changelog version with the standard footer'
}

if [[ "$MODE" == static ]]; then
    printf '1..5\n'
    syntax_test
    render_test
    profile_json_test
    command_surface_test
    release_notes_test
    exit 0
fi

printf '1..5\n'
syntax_test
render_test
profile_json_test
command_surface_test
release_notes_test
