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
        --eject-script "$ROOT/bin/btrfs-backup-eject" \
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

if [[ "$MODE" == static ]]; then
    printf '1..4\n'
    syntax_test
    render_test
    profile_json_test
    status_writer_cli_test
    exit 0
fi

printf '1..4\n'
syntax_test
render_test
profile_json_test
status_writer_cli_test
