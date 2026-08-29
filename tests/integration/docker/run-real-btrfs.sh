#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE_NAME="${IMAGE_NAME:-btrfs-backup-real-test:local}"
BUILD_JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
CONTAINER_WORKDIR=/work
CONTAINER_ID=""

cleanup() {
    if [[ -n "$CONTAINER_ID" ]]; then
        docker rm -f "$CONTAINER_ID" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

usage() {
    cat <<'USAGE'
Usage: tests/integration/docker/run-real-btrfs.sh

Build and run the real Btrfs integration test in a privileged Docker container.
The test creates disposable loop-backed Btrfs and LUKS filesystems inside the
container and does not use host backup configuration.
USAGE
}

case "${1:-}" in
    "") ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        printf 'Unknown option: %s\n' "$1" >&2
        exit 2
        ;;
esac

docker build \
    -t "$IMAGE_NAME" \
    -f "$ROOT/tests/integration/docker/Dockerfile" \
    "$ROOT/tests/integration/docker"

CONTAINER_ID="$(docker run -d --rm --privileged \
    --cgroupns=host \
    -e BUILD_JOBS="$BUILD_JOBS" \
    --tmpfs /run \
    --tmpfs /tmp:exec,mode=1777 \
    -v "$ROOT:$CONTAINER_WORKDIR:ro" \
    -w "$CONTAINER_WORKDIR" \
    "$IMAGE_NAME" \
    /sbin/init)"

docker exec \
    -e BUILD_JOBS="$BUILD_JOBS" \
    -w "$CONTAINER_WORKDIR" \
    "$CONTAINER_ID" \
    "$CONTAINER_WORKDIR/tests/integration/docker/real-btrfs-test.sh"
