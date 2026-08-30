#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail
export LC_ALL=C

if (( EUID != 0 )); then
    printf 'real restore engine test must run as root\n' >&2
    exit 1
fi
if (( $# != 1 )); then
    printf 'usage: %s /path/to/btrfs-backupctl\n' "$0" >&2
    exit 2
fi

CTL="$(realpath -- "$1")"
TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-restore-real.XXXXXX)"
IMAGE="$TEST_ROOT/filesystem.img"
MOUNT="$TEST_ROOT/mount"
LOOP=""

cleanup() {
    set +e
    mountpoint -q "$MOUNT" && umount "$MOUNT"
    [[ -n "$LOOP" ]] && losetup -d "$LOOP" 2>/dev/null
    rm -rf -- "$TEST_ROOT"
}
trap cleanup EXIT

truncate -s 512M "$IMAGE"
LOOP="$(losetup --find --show "$IMAGE")"
mkfs.btrfs -q -f "$LOOP"
mkdir -p "$MOUNT"
mount -o noatime "$LOOP" "$MOUNT"

btrfs subvolume create "$MOUNT/source" >/dev/null
mkdir -p "$MOUNT/source/Documents" "$MOUNT/repository/hosts/host/profiles/default/sources/home"
printf 'restore engine\n' > "$MOUNT/source/Documents/report.txt"
chmod 0640 "$MOUNT/source/Documents/report.txt"
btrfs subvolume snapshot -r \
    "$MOUNT/source" \
    "$MOUNT/repository/hosts/host/profiles/default/sources/home/snapshot" >/dev/null

SNAPSHOT="$MOUNT/repository/hosts/host/profiles/default/sources/home/snapshot"
SNAPSHOT_UUID="$(btrfs subvolume show "$SNAPSHOT" | sed -n 's/^[[:space:]]*UUID:[[:space:]]*//p' | head -n1)"
CREATED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
cat > "$MOUNT/repository/repository.json" <<EOF_REPOSITORY
{"schemaVersion":1,"repositoryId":"real-restore","targetFilesystemUuid":"test-filesystem","createdAt":"$CREATED_AT","features":["catalog-v1"]}
EOF_REPOSITORY
cat > "$MOUNT/repository/catalog.json" <<EOF_CATALOG
{"schemaVersion":1,"generation":1,"snapshots":[{"snapshotId":"snapshot","hostId":"host","profileId":"default","sourceId":"home","relativePath":"hosts/host/profiles/default/sources/home/snapshot","createdAt":"$CREATED_AT","uuid":"$SNAPSHOT_UUID","verified":true}]}
EOF_CATALOG

"$CTL" restore plan \
    --repository "$MOUNT/repository" \
    --snapshot snapshot \
    --source . \
    --destination "$MOUNT/restored" \
    --transaction real-plan \
    --subvolume >/dev/null
[[ ! -e "$MOUNT/restored" ]]

"$CTL" restore execute \
    --repository "$MOUNT/repository" \
    --snapshot snapshot \
    --source . \
    --destination "$MOUNT/restored" \
    --transaction real-execute \
    --subvolume >/dev/null
btrfs subvolume show "$MOUNT/restored" >/dev/null
diff -qr "$SNAPSHOT" "$MOUNT/restored" >/dev/null
[[ "$(stat -c %a "$MOUNT/restored/Documents/report.txt")" == 640 ]]

"$CTL" restore drill \
    --repository "$MOUNT/repository" \
    --snapshot snapshot \
    --source Documents \
    --destination "$MOUNT/drill/result" \
    --transaction real-drill >/dev/null
[[ ! -e "$MOUNT/drill/result" ]]
[[ ! -e "$MOUNT/drill/.btrfs-backup-restore-real-drill.staging" ]]

printf 'ok - real Btrfs restore engine test passed\n'
