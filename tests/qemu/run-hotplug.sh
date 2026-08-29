#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail
export LC_ALL=C

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE_NAME="${IMAGE_NAME:-btrfs-backup-real-test:local}"
QEMU_IMAGE_NAME="${QEMU_IMAGE_NAME:-btrfs-backup-qemu-test:local}"
BUILD_IMAGE_NAME="${BUILD_IMAGE_NAME:-btrfs-backup-build-test:local}"
PACKAGE_BUILDER="${PACKAGE_BUILDER:-local}"
QEMU_CACHE_DIR="${QEMU_CACHE_DIR:-$ROOT/build/qemu-cache}"

case "${1:-}" in
    "") ;;
    -h|--help)
        cat <<'EOF_USAGE'
Usage: tests/qemu/run-hotplug.sh

Build and boot a disposable Arch guest, then attach a virtual LUKS USB disk and
verify that udev starts btrfs-backup@default.service without a graphical target.
Non-root callers need permission to run privileged Docker containers.
EOF_USAGE
        exit 0
        ;;
    *)
        printf 'Unknown option: %s\n' "$1" >&2
        exit 2
        ;;
esac

if (( EUID != 0 )); then
    remove_package_root=0
    if [[ -n "${PACKAGE_DIR:-}" ]]; then
        package_dir="$(realpath -- "$PACKAGE_DIR")"
    else
        package_root="$(mktemp -d /tmp/btrfs-backup-qemu-packages.XXXXXX)"
        package_dir="$package_root/dist"
        remove_package_root=1
    fi
    cleanup_outer() {
        if (( remove_package_root )); then
            rm -rf -- "$package_root"
        fi
    }
    trap cleanup_outer EXIT
    command -v docker >/dev/null 2>&1 || {
        printf '%s\n' 'not ok - Docker is required to run the QEMU test without sudo' >&2
        exit 1
    }
    docker build \
        -t "$IMAGE_NAME" \
        -f "$ROOT/tests/integration/docker/Dockerfile" \
        "$ROOT/tests/integration/docker"
    docker build \
        --build-arg "BASE_IMAGE=$IMAGE_NAME" \
        -t "$QEMU_IMAGE_NAME" \
        -f "$ROOT/tests/qemu/Dockerfile" \
        "$ROOT/tests/qemu"
    if (( remove_package_root )); then
        case "$PACKAGE_BUILDER" in
            local)
                "$ROOT/tools/build-release.sh" \
                    --target arch-base \
                    --skip-tests \
                    --build-dir "$ROOT/build/integration-package" \
                    --dist-dir "$package_dir"
                ;;
            docker)
                docker build \
                    -t "$BUILD_IMAGE_NAME" \
                    -f "$ROOT/tests/integration/docker/Dockerfile.build" \
                    "$ROOT/tests/integration/docker"
                docker run --rm --network=none \
                    --user "$(id -u):$(id -g)" \
                    --tmpfs "/run:uid=$(id -u),gid=$(id -g),mode=0755" \
                    -e BUILD_JOBS="${BUILD_JOBS:-$(nproc)}" \
                    -e HOME=/tmp \
                    -v "$ROOT:/work:ro" \
                    -v "$package_root:/artifacts" \
                    -w /work \
                    "$BUILD_IMAGE_NAME" \
                    /work/tools/build-release.sh \
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
    compgen -G "$package_dir/btrfs-backup-[0-9]*.pkg.tar.zst" >/dev/null \
        || { printf 'No base Arch package found in %s\n' "$package_dir" >&2; exit 1; }
    install -d -m0755 "$QEMU_CACHE_DIR"
    qemu_image_id="$(docker image inspect --format '{{.Id}}' "$QEMU_IMAGE_NAME")"
    qemu_cache_key="${qemu_image_id#sha256:}"
    runtime_image_id="$(docker image inspect --format '{{.Id}}' "$IMAGE_NAME")"
    runtime_cache_key="${runtime_image_id#sha256:}"
    guest_root_tar="$QEMU_CACHE_DIR/guest-root-${runtime_cache_key}.tar"
    if [[ ! -f "$QEMU_CACHE_DIR/rootfs-v2-${qemu_cache_key}.img" && ! -f "$guest_root_tar" ]]; then
        guest_root_tar_temp="$guest_root_tar.tmp"
        container_id="$(docker create "$IMAGE_NAME" /usr/bin/true)"
        trap 'docker rm -f "$container_id" >/dev/null 2>&1 || true; rm -f -- "$guest_root_tar_temp"' EXIT
        docker export --output "$guest_root_tar_temp" "$container_id"
        docker rm "$container_id" >/dev/null
        container_id=""
        mv "$guest_root_tar_temp" "$guest_root_tar"
        trap cleanup_outer EXIT
    fi
    docker run --rm --privileged --network=none \
        -e QEMU_ROOTFS_CACHE_DIR=/qemu-cache \
        -e "QEMU_ROOTFS_CACHE_KEY=$qemu_cache_key" \
        -e "QEMU_ROOTFS_TAR=/qemu-cache/$(basename -- "$guest_root_tar")" \
        -e QEMU_PACKAGE_DIR=/packages \
        -v "$ROOT:/work:ro" \
        -v "$QEMU_CACHE_DIR:/qemu-cache" \
        -v "$package_dir:/packages:ro" \
        -w /work \
        "$QEMU_IMAGE_NAME" \
        /work/tests/qemu/run-hotplug.sh
    rm -f -- "$guest_root_tar"
    exit
fi

TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-qemu.XXXXXX)"
ROOTFS_CACHE_SCHEMA=2
ROOTFS_CACHE_DIR="${QEMU_ROOTFS_CACHE_DIR:-/tmp/btrfs-backup-qemu-cache}"
ROOTFS_CACHE_KEY="${QEMU_ROOTFS_CACHE_KEY:-local}"
ROOT_IMAGE="$ROOTFS_CACHE_DIR/rootfs-v${ROOTFS_CACHE_SCHEMA}-${ROOTFS_CACHE_KEY}.img"
ROOT_IMAGE_TEMP=""
SETUP_IMAGE="$TEST_ROOT/setup.img"
TARGET_IMAGE="$TEST_ROOT/target.img"
ROOT_MOUNT="$TEST_ROOT/root"
CONSOLE_LOG="$TEST_ROOT/console.log"
QMP_SOCKET="$TEST_ROOT/qmp.sock"
QMP_LOG="$TEST_ROOT/qmp.log"
KERNEL_RELEASE="$(uname -r)"
KERNEL_IMAGE="/usr/lib/modules/$KERNEL_RELEASE/vmlinuz"
if [[ ! -r "$KERNEL_IMAGE" && -r /boot/vmlinuz-linux ]]; then
    KERNEL_IMAGE=/boot/vmlinuz-linux
    KERNEL_RELEASE="$(find /usr/lib/modules -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V | tail -n1)"
fi
CONTAINER_ID=""
QEMU_PID=""
ROOT_MOUNTED=0

cleanup() {
    set +e
    if [[ -n "$QEMU_PID" ]]; then
        kill "$QEMU_PID" 2>/dev/null
        wait "$QEMU_PID" 2>/dev/null
    fi
    if (( ROOT_MOUNTED )); then
        umount "$ROOT_MOUNT" 2>/dev/null
    fi
    [[ -z "$ROOT_IMAGE_TEMP" ]] || rm -f -- "$ROOT_IMAGE_TEMP"
    if [[ -n "$CONTAINER_ID" ]]; then
        docker rm -f "$CONTAINER_ID" >/dev/null 2>&1
    fi
    rm -rf -- "$TEST_ROOT"
}
trap cleanup EXIT

fail() {
    printf 'not ok - %s\n' "$1" >&2
    [[ -f "$CONSOLE_LOG" ]] && tail -n 200 "$CONSOLE_LOG" >&2
    exit 1
}

for command_name in cryptsetup flock mkfs.ext4 qemu-system-x86_64 socat tar; do
    command -v "$command_name" >/dev/null 2>&1 || fail "missing command: $command_name"
done
if [[ -z "${QEMU_ROOTFS_TAR:-}" && -z "${QEMU_ROOTFS_FROM_CONTAINER:-}" ]]; then
    command -v docker >/dev/null 2>&1 || fail 'missing command: docker'
fi
[[ -r "$KERNEL_IMAGE" ]] || fail "host kernel image is not readable: $KERNEL_IMAGE"
[[ -d "/usr/lib/modules/$KERNEL_RELEASE" ]] || fail "host kernel modules are unavailable"

PACKAGE_DIR="${QEMU_PACKAGE_DIR:-$TEST_ROOT/packages}"
if [[ -z "${QEMU_PACKAGE_DIR:-}" ]]; then
    "$ROOT/tools/build-release.sh" \
        --target arch-base \
        --skip-tests \
        --dist-dir "$PACKAGE_DIR" >/dev/null
fi

install -d -m0755 "$ROOTFS_CACHE_DIR" "$ROOT_MOUNT"
exec 9>"$ROOTFS_CACHE_DIR/rootfs-v${ROOTFS_CACHE_SCHEMA}-${ROOTFS_CACHE_KEY}.lock"
flock 9
if [[ ! -f "$ROOT_IMAGE" ]]; then
    ROOT_IMAGE_TEMP="$(mktemp "$ROOTFS_CACHE_DIR/.rootfs.XXXXXX.img")"
    truncate -s 6G "$ROOT_IMAGE_TEMP"
    mkfs.ext4 -q -F "$ROOT_IMAGE_TEMP"
    mount -o loop "$ROOT_IMAGE_TEMP" "$ROOT_MOUNT"
    ROOT_MOUNTED=1

    if [[ -n "${QEMU_ROOTFS_TAR:-}" ]]; then
        tar -xpf "$QEMU_ROOTFS_TAR" -C "$ROOT_MOUNT"
    elif [[ -n "${QEMU_ROOTFS_FROM_CONTAINER:-}" ]]; then
        tar \
            --one-file-system \
            --exclude='./tmp' \
            --exclude='./work' \
            --exclude='./qemu-cache' \
            -cpf - \
            -C / . \
            | tar -xpf - -C "$ROOT_MOUNT"
    else
        docker build \
            -t "$IMAGE_NAME" \
            -f "$ROOT/tests/integration/docker/Dockerfile" \
            "$ROOT/tests/integration/docker" >/dev/null
        CONTAINER_ID="$(docker create "$IMAGE_NAME" /usr/bin/true)"
        docker export "$CONTAINER_ID" | tar -xpf - -C "$ROOT_MOUNT"
        docker rm "$CONTAINER_ID" >/dev/null
        CONTAINER_ID=""
    fi

    install -d -m0755 \
        "$ROOT_MOUNT/usr/lib/modules" \
        "$ROOT_MOUNT/etc/systemd/system/multi-user.target.wants"
    cp -a "/usr/lib/modules/$KERNEL_RELEASE" "$ROOT_MOUNT/usr/lib/modules/"
    rm -f -- "$ROOT_MOUNT/.dockerenv" "$ROOT_MOUNT/run/systemd/container"
    printf '%s\n' 'btrfs-backup-qemu' > "$ROOT_MOUNT/etc/hostname"
    : > "$ROOT_MOUNT/etc/machine-id"
    cat > "$ROOT_MOUNT/etc/systemd/system/qemu-test-setup.service" <<'EOF_SETUP_UNIT'
[Unit]
Description=Install the QEMU test payload
After=systemd-udevd.service dev-vdb.device
Requires=dev-vdb.device

[Service]
Type=oneshot
ExecStart=/usr/bin/mkdir -p /run/qemu-test-setup
ExecStart=/usr/bin/mount -o ro /dev/vdb /run/qemu-test-setup
ExecStart=/usr/bin/sh /run/qemu-test-setup/setup.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF_SETUP_UNIT
    ln -s ../qemu-test-setup.service \
        "$ROOT_MOUNT/etc/systemd/system/multi-user.target.wants/qemu-test-setup.service"
    ln -sfn /usr/lib/systemd/system/multi-user.target \
        "$ROOT_MOUNT/etc/systemd/system/default.target"
    umount "$ROOT_MOUNT"
    ROOT_MOUNTED=0
    mv "$ROOT_IMAGE_TEMP" "$ROOT_IMAGE"
    ROOT_IMAGE_TEMP=""
fi
flock -u 9

truncate -s 128M "$SETUP_IMAGE"
mkfs.ext4 -q -F "$SETUP_IMAGE"
mount -o loop "$SETUP_IMAGE" "$ROOT_MOUNT"
ROOT_MOUNTED=1
cp "$PACKAGE_DIR"/btrfs-backup-[0-9]*.pkg.tar.zst "$ROOT_MOUNT/package.pkg.tar.zst"

printf '%s\n' 'qemu-hotplug-test-key' > "$TEST_ROOT/luks.key"
chmod 0600 "$TEST_ROOT/luks.key"
truncate -s 64M "$TARGET_IMAGE"
cryptsetup luksFormat --type luks2 --pbkdf pbkdf2 --pbkdf-force-iterations 1000 \
    --batch-mode --key-file "$TEST_ROOT/luks.key" "$TARGET_IMAGE"
TARGET_UUID="$(cryptsetup luksUUID "$TARGET_IMAGE")"

cat > "$ROOT_MOUNT/setup.sh" <<EOF_SETUP
set -eu
tar --zstd -xpf /run/qemu-test-setup/package.pkg.tar.zst -C /
install -d -m0755 \\
    /etc/btrfs-backup \\
    /etc/udev/rules.d \\
    /etc/systemd/system/btrfs-backup@default.service.d
cat > /etc/udev/rules.d/99-btrfs-backup-default.rules <<'EOF_RULE'
ACTION=="add", SUBSYSTEM=="block", ENV{ID_FS_TYPE}=="crypto_LUKS", ENV{ID_FS_UUID}=="$TARGET_UUID", TAG+="systemd", ENV{SYSTEMD_WANTS}+="btrfs-backup@default.service"
EOF_RULE

cat > /etc/systemd/system/btrfs-backup@default.service.d/qemu-hotplug-test.conf <<'EOF_OVERRIDE'
[Unit]
OnSuccess=
OnFailure=

[Service]
ExecStart=
ExecStart=/usr/bin/sh -c 'if /usr/bin/systemctl is-active --quiet graphical.target; then exit 1; fi; printf "QEMU_HOTPLUG_OK\n" > /dev/ttyS0'
EOF_OVERRIDE
systemctl daemon-reload
udevadm control --reload
! systemd-detect-virt --container >/dev/null 2>&1
! systemctl is-active --quiet graphical.target
printf 'QEMU_READY\n' > /dev/ttyS0
EOF_SETUP
chmod 0755 "$ROOT_MOUNT/setup.sh"
umount "$ROOT_MOUNT"
ROOT_MOUNTED=0

qemu_args=(
    -machine q35
    -m 768
    -smp 2
    -nographic
    -no-reboot
    -kernel "$KERNEL_IMAGE"
    -append "root=/dev/vda rw rootfstype=ext4 console=ttyS0 systemd.unit=multi-user.target"
    -drive "file=$ROOT_IMAGE,if=virtio,format=raw,snapshot=on"
    -drive "file=$SETUP_IMAGE,if=virtio,format=raw,readonly=on"
    -device qemu-xhci,id=xhci
    -drive "file=$TARGET_IMAGE,if=none,format=raw,id=hotplug-target"
    -qmp "unix:$QMP_SOCKET,server=on,wait=off"
    -serial "file:$CONSOLE_LOG"
    -monitor none
    -nic none
)
if [[ -r /dev/kvm && -w /dev/kvm ]]; then
    qemu_args=(-enable-kvm "${qemu_args[@]}")
else
    qemu_args=(-accel tcg "${qemu_args[@]}")
fi

qemu-system-x86_64 "${qemu_args[@]}" &
QEMU_PID=$!
for _ in $(seq 1 180); do
    grep -Fq 'QEMU_READY' "$CONSOLE_LOG" 2>/dev/null && break
    kill -0 "$QEMU_PID" 2>/dev/null || fail 'QEMU guest exited before reaching multi-user.target'
    sleep 0.25
done
grep -Fq 'QEMU_READY' "$CONSOLE_LOG" || fail 'QEMU guest did not become ready'

{
    printf '%s\n' '{"execute":"qmp_capabilities"}'
    printf '%s\n' '{"execute":"device_add","arguments":{"driver":"usb-storage","drive":"hotplug-target","id":"target-usb"}}'
} | socat -t 5 - "UNIX-CONNECT:$QMP_SOCKET" > "$QMP_LOG"
if (( $(grep -Fc '"return": {}' "$QMP_LOG") < 2 )); then
    cat "$QMP_LOG" >&2
    fail 'QMP rejected the virtual USB attachment'
fi

for _ in $(seq 1 60); do
    grep -Fq 'QEMU_HOTPLUG_OK' "$CONSOLE_LOG" && break
    kill -0 "$QEMU_PID" 2>/dev/null || fail 'QEMU guest exited after target attachment'
    sleep 0.25
done
grep -Fq 'QEMU_HOTPLUG_OK' "$CONSOLE_LOG" \
    || fail 'udev did not start btrfs-backup@default.service after USB attachment'

printf '%s\n' 'ok - QEMU USB hotplug starts the system runner without a graphical session'
