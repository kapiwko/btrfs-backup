#!/usr/bin/env bash
set -Eeuo pipefail
export LC_ALL=C

if (( $# != 1 )); then
    printf 'Usage: %s UNIT_FILE\n' "$0" >&2
    exit 2
fi

UNIT_FILE="$1"
THRESHOLD="${SYSTEMD_SECURITY_THRESHOLD:-8}"
TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-systemd-security.XXXXXX)"
UNIT_NAME=btrfs-backup@.service
STAGED_UNIT="$TEST_ROOT/$UNIT_NAME"

cleanup() {
    rm -rf -- "$TEST_ROOT"
}
trap cleanup EXIT

sed \
    -e 's#{{BACKUP_COMMAND}}#/usr/bin/true#g' \
    -e 's#{{EJECT_SCRIPT_PATH}}#/usr/bin/true#g' \
    -e 's#{{PROFILE_ID}}#default#g' \
    "$UNIT_FILE" > "$STAGED_UNIT"

for directive in \
    NoNewPrivileges=yes \
    PrivateTmp=yes \
    ProtectSystem=full \
    ProtectKernelTunables=yes \
    ProtectKernelModules=yes \
    ProtectControlGroups=yes \
    ProtectHostname=yes \
    ProtectClock=yes \
    ProtectProc=invisible \
    LockPersonality=yes \
    RestrictRealtime=yes \
    MemoryDenyWriteExecute=yes \
    SystemCallArchitectures=native \
    Environment=PATH=/usr/bin \
    'RestrictAddressFamilies=AF_UNIX AF_NETLINK'; do
    grep -Fxq -- "$directive" "$STAGED_UNIT" || {
        printf 'Missing systemd hardening directive: %s\n' "$directive" >&2
        exit 1
    }
done

security_output="$(
    SYSTEMD_UNIT_PATH="$TEST_ROOT" systemd-analyze security \
        --offline=yes \
        --instance=default \
        --threshold=100 \
        --no-pager \
        "$UNIT_NAME"
)"
printf '%s\n' "$security_output"

exposure="$(sed -n 's/.*Overall exposure level.*: \([0-9][0-9.]*\) .*/\1/p' <<< "$security_output")"
if [[ -z "$exposure" ]] || ! awk -v exposure="$exposure" -v threshold="$THRESHOLD" \
    'BEGIN { exit !(exposure <= threshold) }'; then
    printf 'systemd exposure %s exceeds threshold %s\n' "${exposure:-unknown}" "$THRESHOLD" >&2
    exit 1
fi
