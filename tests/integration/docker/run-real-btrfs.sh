#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE_NAME="${IMAGE_NAME:-btrfs-backup-real-test:local}"
BUILD_IMAGE_NAME="${BUILD_IMAGE_NAME:-btrfs-backup-build-test:local}"
PACKAGE_BUILDER="${PACKAGE_BUILDER:-local}"
BUILD_JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
CONTAINER_WORKDIR=/work
CONTAINER_ID=""
PACKAGE_ROOT=""
PACKAGE_TEMP_ROOT=""
BROWSE_SESSION_CLIENT="${BTRFSBACKUP_BROWSE_SESSION_CLIENT:-$ROOT/build/tests/integration/btrfsbackup-integration-browse-session-client}"
DEVICE_PROVISIONING_CLIENT="${BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT:-$ROOT/build/tests/integration/btrfsbackup-integration-device-provisioning-client}"
REAL_BTRFS_TESTS="${BTRFSBACKUP_REAL_BTRFS_TESTS:-$ROOT/build/tests/integration/btrfsbackup-real-btrfs-tests}"

cleanup() {
    if [[ -n "$CONTAINER_ID" ]]; then
        docker rm -f "$CONTAINER_ID" >/dev/null 2>&1 || true
    fi
    if [[ -n "$PACKAGE_TEMP_ROOT" ]]; then
        rm -rf -- "$PACKAGE_TEMP_ROOT"
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

BROWSE_SESSION_CLIENT="$(realpath -- "$BROWSE_SESSION_CLIENT")"
DEVICE_PROVISIONING_CLIENT="$(realpath -- "$DEVICE_PROVISIONING_CLIENT")"
REAL_BTRFS_TESTS="$(realpath -- "$REAL_BTRFS_TESTS")"
[[ -x "$BROWSE_SESSION_CLIENT" ]] || {
    printf 'Browse-session integration client is not executable: %s\n' "$BROWSE_SESSION_CLIENT" >&2
    exit 1
}
[[ -x "$DEVICE_PROVISIONING_CLIENT" ]] || {
    printf 'Device-provisioning integration client is not executable: %s\n' "$DEVICE_PROVISIONING_CLIENT" >&2
    exit 1
}
[[ -x "$REAL_BTRFS_TESTS" ]] || {
    printf 'Real-Btrfs C++ integration test is not executable: %s\n' "$REAL_BTRFS_TESTS" >&2
    exit 1
}

docker build \
    -t "$IMAGE_NAME" \
    -f "$ROOT/tests/integration/docker/Dockerfile" \
    "$ROOT/tests/integration/docker"

if [[ -n "${PACKAGE_DIR:-}" ]]; then
    PACKAGE_ROOT="$(realpath -- "$PACKAGE_DIR")"
else
    PACKAGE_TEMP_ROOT="$(mktemp -d /tmp/btrfs-backup-real-packages.XXXXXX)"
    PACKAGE_ROOT="$PACKAGE_TEMP_ROOT/dist"
    case "$PACKAGE_BUILDER" in
        local)
            "$ROOT/tools/build-release.sh" \
                --target arch-base \
                --skip-tests \
                --build-dir "$ROOT/build/integration-package" \
                --dist-dir "$PACKAGE_ROOT"
            ;;
        docker)
            docker build \
                -t "$BUILD_IMAGE_NAME" \
                -f "$ROOT/tests/integration/docker/Dockerfile.build" \
                "$ROOT/tests/integration/docker"
            docker run --rm --network=none \
                --user "$(id -u):$(id -g)" \
                --tmpfs "/run:uid=$(id -u),gid=$(id -g),mode=0755" \
                -e BUILD_JOBS="$BUILD_JOBS" \
                -e HOME=/tmp \
                -v "$ROOT:$CONTAINER_WORKDIR:ro" \
                -v "$PACKAGE_TEMP_ROOT:/artifacts" \
                -w "$CONTAINER_WORKDIR" \
                "$BUILD_IMAGE_NAME" \
                "$CONTAINER_WORKDIR/tools/build-release.sh" \
                --target arch-base \
                --skip-tests \
                --dist-dir /artifacts/dist
            ;;
        *)
            printf 'Unsupported PACKAGE_BUILDER: %s\n' "$PACKAGE_BUILDER" >&2
            exit 2
            ;;
    esac
fi
compgen -G "$PACKAGE_ROOT/btrfs-backup-[0-9]*.pkg.tar.zst" >/dev/null \
    || { printf 'No base Arch package found in %s\n' "$PACKAGE_ROOT" >&2; exit 1; }

CONTAINER_ID="$(docker run -d --rm --privileged \
    --cgroupns=host \
    -e BUILD_JOBS="$BUILD_JOBS" \
    --tmpfs /run \
    --tmpfs /tmp:exec,mode=1777 \
    -v "$ROOT:$CONTAINER_WORKDIR:ro" \
    -v "$PACKAGE_ROOT:/packages:ro" \
    -v "$BROWSE_SESSION_CLIENT:/opt/btrfsbackup-browse-session-client:ro" \
    -v "$DEVICE_PROVISIONING_CLIENT:/opt/btrfsbackup-device-provisioning-client:ro" \
    -v "$REAL_BTRFS_TESTS:/opt/btrfsbackup-real-btrfs-tests:ro" \
    -w "$CONTAINER_WORKDIR" \
    "$IMAGE_NAME" \
    /sbin/init)"

docker exec \
    -e BUILD_JOBS="$BUILD_JOBS" \
    -e BTRFSBACKUP_PACKAGE_DIR=/packages \
    -e BTRFSBACKUP_BROWSE_SESSION_CLIENT=/opt/btrfsbackup-browse-session-client \
    -e BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT=/opt/btrfsbackup-device-provisioning-client \
    -e BTRFSBACKUP_REAL_BTRFS_TESTS=/opt/btrfsbackup-real-btrfs-tests \
    -w "$CONTAINER_WORKDIR" \
    "$CONTAINER_ID" \
    "$CONTAINER_WORKDIR/tests/integration/docker/real-btrfs-test.sh"
