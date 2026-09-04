#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail
export LC_ALL=C

if (( $# < 1 || $# > 2 )); then
    printf 'Usage: %s UNIT_FILE [backup|device-preparation]\n' "$0" >&2
    exit 2
fi

UNIT_FILE="$1"
POLICY="${2:-backup}"
TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-systemd-security.XXXXXX)"
UNIT_NAME="$(basename -- "$UNIT_FILE" .example)"
STAGED_UNIT="$TEST_ROOT/$UNIT_NAME"

cleanup() {
    rm -rf -- "$TEST_ROOT"
}
trap cleanup EXIT

sed \
    -e 's#@BTRFSBACKUP_BACKUP_COMMAND@#/usr/bin/true#g' \
    -e 's#@BTRFSBACKUP_DEVICE_PREPARATION_EXECUTABLE@#/usr/bin/true#g' \
    -e 's#@BTRFSBACKUP_EJECT_SCRIPT_PATH@#/usr/bin/true#g' \
    -e 's#{{PROFILE_ID}}#default#g' \
    "$UNIT_FILE" > "$STAGED_UNIT"

common_directives=(
    NoNewPrivileges=yes
    PrivateTmp=yes
    ProtectKernelTunables=yes
    ProtectKernelModules=yes
    ProtectControlGroups=yes
    ProtectHostname=yes
    ProtectClock=yes
    ProtectProc=invisible
    LockPersonality=yes
    RestrictRealtime=yes
    MemoryDenyWriteExecute=yes
    SystemCallArchitectures=native
    Environment=PATH=/usr/bin
    'RestrictAddressFamilies=AF_UNIX AF_NETLINK'
)

case "$POLICY" in
    backup)
        threshold="${SYSTEMD_SECURITY_THRESHOLD:-8}"
        policy_directives=(
            ProtectSystem=full
        )
        ;;
    device-preparation)
        threshold="${SYSTEMD_DEVICE_PREPARATION_SECURITY_THRESHOLD:-4.5}"
        policy_directives=(
            'Wants=modprobe@dm_mod.service modprobe@dm_crypt.service'
            'After=systemd-udevd.service modprobe@dm_mod.service modprobe@dm_crypt.service'
            User=root
            Group=root
            UMask=0077
            PrivateMounts=yes
            ProtectSystem=strict
            ProtectHome=read-only
            ProtectKernelLogs=yes
            RestrictNamespaces=yes
            DevicePolicy=closed
            'DeviceAllow=/dev/mapper/control rw'
            'CapabilityBoundingSet=CAP_SYS_ADMIN CAP_DAC_OVERRIDE CAP_FOWNER'
        )
        ;;
    *)
        printf 'Unknown systemd security policy: %s\n' "$POLICY" >&2
        exit 2
        ;;
esac

for directive in "${common_directives[@]}" "${policy_directives[@]}"; do
    grep -Fxq -- "$directive" "$STAGED_UNIT" || {
        printf 'Missing %s systemd hardening directive: %s\n' "$POLICY" "$directive" >&2
        exit 1
    }
done
if [[ "$POLICY" == device-preparation ]] && grep -Fxq -- 'DeviceAllow=block-* rw' "$STAGED_UNIT"; then
    printf 'Device preparation must not grant access to every block device\n' >&2
    exit 1
fi

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
if [[ -z "$exposure" ]] || ! awk -v exposure="$exposure" -v threshold="$threshold" \
    'BEGIN { exit !(exposure <= threshold) }'; then
    printf '%s systemd exposure %s exceeds threshold %s\n' \
        "$POLICY" "${exposure:-unknown}" "$threshold" >&2
    exit 1
fi
