#!/usr/bin/env bash
set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE_NAME="${IMAGE_NAME:-btrfs-backup-real-test:local}"
CONTAINER_WORKDIR=/work

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
    "$ROOT"

docker run --rm --privileged \
    --tmpfs /run \
    --tmpfs /tmp:exec,mode=1777 \
    -v "$ROOT:$CONTAINER_WORKDIR:ro" \
    -w "$CONTAINER_WORKDIR" \
    "$IMAGE_NAME" \
    "$CONTAINER_WORKDIR/tests/integration/docker/real-btrfs-test.sh"

