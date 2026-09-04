#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -Eeuo pipefail
export LC_ALL=C

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE_NAME="${IMAGE_NAME:-btrfs-backup-real-test:local}"
DEVICE_PROVISIONING_CLIENT="${BTRFSBACKUP_DEVICE_PROVISIONING_CLIENT:?missing device-provisioning integration client}"
(( EUID == 0 )) || { printf "%s\\n" "QEMU inner runner requires root" >&2; exit 1; }

TEST_ROOT="$(mktemp -d /tmp/btrfs-backup-qemu.XXXXXX)"
ROOTFS_CACHE_SCHEMA=2
ROOTFS_CACHE_DIR="${QEMU_ROOTFS_CACHE_DIR:-/tmp/btrfs-backup-qemu-cache}"
ROOTFS_CACHE_KEY="${QEMU_ROOTFS_CACHE_KEY:-local}"
ROOT_IMAGE="$ROOTFS_CACHE_DIR/rootfs-v${ROOTFS_CACHE_SCHEMA}-${ROOTFS_CACHE_KEY}.img"
ROOT_IMAGE_TEMP=""
SETUP_IMAGE="$TEST_ROOT/setup.img"
TARGET_IMAGE="$TEST_ROOT/target.img"
SOURCE_IMAGE="$TEST_ROOT/source.img"
WHOLE_DEVICE_IMAGE="$TEST_ROOT/whole-device.img"
PARTITION_DEVICE_IMAGE="$TEST_ROOT/partition-device.img"
MANAGER_KILL_IMAGE="$TEST_ROOT/manager-kill.img"
HELPER_KILL_IMAGE="$TEST_ROOT/helper-kill.img"
POWER_LOSS_IMAGE="$TEST_ROOT/power-loss.img"
UNPLUG_IMAGE="$TEST_ROOT/unplug.img"
REPLACEMENT_IMAGE="$TEST_ROOT/replacement.img"
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

for command_name in cryptsetup flock mkfs.btrfs mkfs.ext4 qemu-system-x86_64 sfdisk sha256sum socat tar; do
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
cp "$DEVICE_PROVISIONING_CLIENT" "$ROOT_MOUNT/device-provisioning-client"

printf '%s\n' 'qemu-hotplug-test-key' > "$TEST_ROOT/luks.key"
chmod 0600 "$TEST_ROOT/luks.key"
cp "$TEST_ROOT/luks.key" "$ROOT_MOUNT/provisioning.key"
truncate -s 64M "$TARGET_IMAGE"
cryptsetup luksFormat --type luks2 --pbkdf pbkdf2 --pbkdf-force-iterations 1000 \
    --batch-mode --key-file "$TEST_ROOT/luks.key" "$TARGET_IMAGE"
TARGET_UUID="$(cryptsetup luksUUID "$TARGET_IMAGE")"
TARGET_DEVICE_UNIT="$(systemd-escape --path --suffix=device "/dev/disk/by-uuid/$TARGET_UUID")"

truncate -s 384M "$SOURCE_IMAGE"
mkfs.btrfs -q -f "$SOURCE_IMAGE"
truncate -s 512M "$WHOLE_DEVICE_IMAGE"
mkfs.ext4 -q -F -L QEMU-WHOLE "$WHOLE_DEVICE_IMAGE"
truncate -s 768M "$PARTITION_DEVICE_IMAGE"
printf '%s\n' \
    'label: gpt' \
    'size=128M,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4' \
    'size=512M,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4' \
    | sfdisk --quiet "$PARTITION_DEVICE_IMAGE"
truncate -s 544M "$MANAGER_KILL_IMAGE"
truncate -s 576M "$HELPER_KILL_IMAGE"
truncate -s 608M "$POWER_LOSS_IMAGE"
truncate -s 640M "$UNPLUG_IMAGE"
truncate -s 672M "$REPLACEMENT_IMAGE"
mkfs.ext4 -q -F -L QEMU-MANAGER-KILL "$MANAGER_KILL_IMAGE"
mkfs.ext4 -q -F -L QEMU-HELPER-KILL "$HELPER_KILL_IMAGE"
mkfs.ext4 -q -F -L QEMU-POWER-LOSS "$POWER_LOSS_IMAGE"
mkfs.ext4 -q -F -L QEMU-UNPLUG "$UNPLUG_IMAGE"
mkfs.ext4 -q -F -L QEMU-REPLACEMENT "$REPLACEMENT_IMAGE"
REPLACEMENT_HASH="$(sha256sum "$REPLACEMENT_IMAGE" | awk '{print $1}')"

python3 "$ROOT/tests/qemu/hotplug_guest_payload.py" \
    --target-uuid "$TARGET_UUID" \
    --target-device-unit "$TARGET_DEVICE_UNIT" \
    --replacement-hash "$REPLACEMENT_HASH" \
    > "$ROOT_MOUNT/setup.sh"
chmod 0755 "$ROOT_MOUNT/setup.sh"
umount "$ROOT_MOUNT"
ROOT_MOUNTED=0

qemu_args=(
    -machine q35
    -m 768
    -smp 2
    -nographic
    -kernel "$KERNEL_IMAGE"
    -append "root=/dev/vda rw rootfstype=ext4 console=ttyS0 systemd.unit=multi-user.target"
    -drive "file=$ROOT_IMAGE,if=none,id=root-disk,format=raw,snapshot=on"
    -device "virtio-blk-pci,drive=root-disk,serial=bb-root"
    -drive "file=$SETUP_IMAGE,if=none,id=setup-disk,format=raw,readonly=on"
    -device "virtio-blk-pci,drive=setup-disk,serial=bb-setup"
    -drive "file=$SOURCE_IMAGE,if=none,id=source-disk,format=raw,snapshot=on"
    -device "virtio-blk-pci,drive=source-disk,serial=bb-source"
    -drive "file=$WHOLE_DEVICE_IMAGE,if=none,id=whole-disk,format=raw,snapshot=on"
    -device "virtio-blk-pci,drive=whole-disk,serial=bb-whole"
    -drive "file=$PARTITION_DEVICE_IMAGE,if=none,id=partition-disk,format=raw,snapshot=on"
    -device "virtio-blk-pci,drive=partition-disk,serial=bb-partition"
    -drive "file=$MANAGER_KILL_IMAGE,if=none,id=manager-kill-disk,format=raw,snapshot=on"
    -device "virtio-blk-pci,drive=manager-kill-disk,serial=bb-manager-kill"
    -drive "file=$HELPER_KILL_IMAGE,if=none,id=helper-kill-disk,format=raw,snapshot=on"
    -device "virtio-blk-pci,drive=helper-kill-disk,serial=bb-helper-kill"
    -drive "file=$POWER_LOSS_IMAGE,if=none,id=power-loss-disk,format=raw,snapshot=on"
    -device "virtio-blk-pci,drive=power-loss-disk,serial=bb-power-loss"
    -device "pcie-root-port,id=provisioning-port,slot=0x10,chassis=10"
    -device "qemu-xhci,id=xhci"
    -blockdev "driver=file,filename=$UNPLUG_IMAGE,node-name=unplug-file"
    -blockdev "driver=raw,file=unplug-file,node-name=unplug-disk"
    -blockdev "driver=file,filename=$REPLACEMENT_IMAGE,node-name=replacement-file"
    -blockdev "driver=raw,file=replacement-file,node-name=replacement-disk"
    -blockdev "driver=file,filename=$TARGET_IMAGE,node-name=hotplug-target-file"
    -blockdev "driver=raw,file=hotplug-target-file,node-name=hotplug-target"
    -qmp "unix:$QMP_SOCKET,server=on,wait=off"
    -serial "file:$CONSOLE_LOG"
    -monitor none
    -nic none
)
if [[ -r /dev/kvm && -w /dev/kvm ]]; then
    qemu_args=(-enable-kvm -cpu host "${qemu_args[@]}")
else
    qemu_args=(-accel tcg -cpu max "${qemu_args[@]}")
fi

qemu-system-x86_64 "${qemu_args[@]}" &
QEMU_PID=$!
wait_for_console() {
    local marker="$1"
    local failure="$2"
    for _ in $(seq 1 1200); do
        grep -Fq "$marker" "$CONSOLE_LOG" 2>/dev/null && return 0
        kill -0 "$QEMU_PID" 2>/dev/null || fail "$failure"
        sleep 0.25
    done
    fail "$failure"
}
qmp_request() {
    local command="$1"
    {
        printf '%s\n' '{"execute":"qmp_capabilities"}'
        printf '%s\n' "$command"
    } | socat -t 5 - "UNIX-CONNECT:$QMP_SOCKET" > "$QMP_LOG"
    if (( $(grep -Fc '"return": {}' "$QMP_LOG") < 2 )); then
        cat "$QMP_LOG" >&2
        fail 'QMP rejected a provisioning failure-injection request'
    fi
}

wait_for_console QEMU_UNPLUG_ATTACH_READY 'guest did not request the provisioning hotplug disk'
qmp_request '{"execute":"device_add","arguments":{"driver":"virtio-blk-pci","drive":"unplug-disk","id":"provisioning-hotplug","serial":"qemu-unplug-original","bus":"provisioning-port"}}'
wait_for_console QEMU_UNPLUG_READY 'guest did not reach the device-unplug boundary'
qmp_request '{"execute":"device_del","arguments":{"id":"provisioning-hotplug"}}'
wait_for_console QEMU_REPLACEMENT_ATTACH_READY 'guest did not reject the unplugged provisioning target'
qmp_request '{"execute":"device_add","arguments":{"driver":"virtio-blk-pci","drive":"replacement-disk","id":"provisioning-hotplug","serial":"qemu-unplug-replacement","bus":"provisioning-port"}}'
wait_for_console QEMU_UNPLUG_RECOVERY_OK 'guest modified or accepted the replacement provisioning device'
wait_for_console QEMU_POWER_LOSS_READY 'guest did not reach the power-loss boundary'
qmp_request '{"execute":"system_reset"}'
wait_for_console QEMU_POWER_LOSS_RECOVERED 'guest did not recover the interrupted transaction after reset'
wait_for_console QEMU_READY 'QEMU guest did not become ready after recovery'
grep -Fq 'QEMU_PROVISIONING_OK' "$CONSOLE_LOG" \
    || fail 'whole-device and existing-partition provisioning did not pass in QEMU'
grep -Fq 'QEMU_MANAGER_KILL_OK' "$CONSOLE_LOG" \
    || fail 'manager SIGKILL recovery did not pass in QEMU'
grep -Fq 'QEMU_HELPER_KILL_OK' "$CONSOLE_LOG" \
    || fail 'helper SIGKILL recovery did not pass in QEMU'

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
grep -Fq 'QEMU_HOTPLUG_OK_1' "$CONSOLE_LOG" \
    || fail 'udev did not start btrfs-backup@default.service after USB attachment'

{
    printf '%s\n' '{"execute":"qmp_capabilities"}'
    printf '%s\n' '{"execute":"device_del","arguments":{"id":"target-usb"}}'
} | socat -t 5 - "UNIX-CONNECT:$QMP_SOCKET" > "$QMP_LOG"
if (( $(grep -Fc '"return": {}' "$QMP_LOG") < 2 )); then
    cat "$QMP_LOG" >&2
    fail 'QMP rejected the virtual USB removal'
fi

for _ in $(seq 1 60); do
    grep -Fq 'QEMU_TARGET_STOP_1' "$CONSOLE_LOG" && break
    kill -0 "$QEMU_PID" 2>/dev/null || fail 'QEMU guest exited after target removal'
    sleep 0.25
done
grep -Fq 'QEMU_TARGET_STOP_1' "$CONSOLE_LOG" \
    || fail 'target activation remained active after USB removal'

{
    printf '%s\n' '{"execute":"qmp_capabilities"}'
    printf '%s\n' '{"execute":"device_add","arguments":{"driver":"usb-storage","drive":"hotplug-target","id":"target-usb"}}'
} | socat -t 5 - "UNIX-CONNECT:$QMP_SOCKET" > "$QMP_LOG"
if (( $(grep -Fc '"return": {}' "$QMP_LOG") < 2 )); then
    cat "$QMP_LOG" >&2
    fail 'QMP rejected the virtual USB reattachment'
fi

for _ in $(seq 1 60); do
    grep -Fq 'QEMU_HOTPLUG_OK_2' "$CONSOLE_LOG" && break
    kill -0 "$QEMU_PID" 2>/dev/null || fail 'QEMU guest exited after target reattachment'
    sleep 0.25
done
grep -Fq 'QEMU_TARGET_START_2' "$CONSOLE_LOG" \
    || fail 'target activation did not restart after USB reattachment'
grep -Fq 'QEMU_HOTPLUG_OK_2' "$CONSOLE_LOG" \
    || fail 'udev did not restart btrfs-backup@default.service after USB reattachment'

printf '%s\n' 'ok - QEMU provisioning, interruption recovery and USB hotplug pass in a system guest'
