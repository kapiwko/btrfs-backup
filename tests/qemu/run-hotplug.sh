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

Build and boot a disposable Arch guest, provision a whole disk and one existing
partition, then attach, disconnect and reconnect a virtual LUKS USB disk. Verify
the destructive operations, udev activation and device lifetime handling.
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
    # shellcheck disable=SC2329 # Invoked indirectly by the EXIT trap.
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

for command_name in cc cryptsetup flock mkfs.btrfs mkfs.ext4 qemu-system-x86_64 sfdisk sha256sum socat tar; do
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
cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/tests/integration/DeviceProvisioningClient.c" \
    -lsystemd \
    -o "$ROOT_MOUNT/device-provisioning-client"

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

cat > "$ROOT_MOUNT/setup.sh" <<EOF_SETUP
set -eu
exec 2>/dev/ttyS0
report_failure() {
    status=\$?
    if test "\$status" -ne 0; then
        printf 'QEMU_SETUP_FAILED status=%s\n' "\$status" > /dev/ttyS0
        systemctl status --no-pager --full btrfs-backupd.service > /dev/ttyS0 2>&1 || true
        journalctl --no-pager -b -u btrfs-backupd.service -n 100 > /dev/ttyS0 2>&1 || true
        journalctl --no-pager -b -t btrfs-backup-device-preparation -n 100 > /dev/ttyS0 2>&1 || true
        for transaction in /var/lib/btrfs-backup/device-preparations/*.json; do
            test -f "\$transaction" || continue
            printf '%s\n' "--- \$transaction ---" > /dev/ttyS0
            operation=\${transaction##*/}
            operation=\${operation%.json}
            systemctl show "btrfs-backup-device-preparation@\$operation.service" \
                -p DevicePolicy -p DeviceAllow -p FragmentPath -p DropInPaths > /dev/ttyS0 2>&1 || true
            cat "\$transaction" > /dev/ttyS0
        done
    fi
}
trap report_failure EXIT
tar --zstd -xpf /run/qemu-test-setup/package.pkg.tar.zst -C /
transaction_file() {
    printf '/var/lib/btrfs-backup/device-preparations/%s.json' "\$1"
}
transaction_state() {
    sed -n 's/.*"state":[[:space:]]*"\([^"]*\)".*/\1/p' "\$(transaction_file "\$1")" | head -n1
}
wait_for_transaction_state() {
    operation="\$1"
    expected="\$2"
    for _ in \$(seq 1 1800); do
        test "\$(transaction_state "\$operation" 2>/dev/null || true)" = "\$expected" && return 0
        sleep 0.1
    done
    return 1
}
operation_id_from() {
    sed -n 's/.*"operationId":[[:space:]]*"\([^"]*\)".*/\1/p' "\$1" | head -n1
}
wait_for_helper_pid() {
    unit="\$1"
    for _ in \$(seq 1 600); do
        pid="\$(systemctl show --property=MainPID --value "\$unit" 2>/dev/null || true)"
        test -n "\$pid" && test "\$pid" != 0 && return 0
        sleep 0.05
    done
    return 1
}
refresh_preparation_status() {
    busctl --system call io.github.btrfsbackup.Manager1 /io/github/btrfsbackup/Manager1 \
        io.github.btrfsbackup.Manager1 GetDevicePreparation s "\$1" >/dev/null
}

power_loss_marker=/var/lib/btrfs-backup/qemu-power-loss-operation
if test -f "\$power_loss_marker"; then
    power_loss_operation="\$(cat "\$power_loss_marker")"
    systemctl start polkit.service btrfs-backupd.service
    wait_for_transaction_state "\$power_loss_operation" interrupted
    grep -Eq '"errorCode":[[:space:]]*"device-preparation\.(daemon-restarted|helper-exited)"' \
        "\$(transaction_file "\$power_loss_operation")"
    for _ in \$(seq 1 600); do
        test ! -e /dev/mapper/btrfs-backup-qemu-power-loss && break
        sleep 0.1
    done
    test ! -e /dev/mapper/btrfs-backup-qemu-power-loss
    grep -Eq '"cleanupResult":[[:space:]]*"(mapper-closed|not-required)"' \
        "\$(transaction_file "\$power_loss_operation")"
    rm -f "\$power_loss_marker"
    printf 'QEMU_POWER_LOSS_RECOVERED\n' > /dev/ttyS0
    printf 'QEMU_READY\n' > /dev/ttyS0
    exit 0
fi
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
Requires=qemu-hotplug-target-holder.service
After=qemu-hotplug-target-holder.service

[Service]
ExecStart=
ExecStart=/usr/bin/sh -c 'if /usr/bin/systemctl is-active --quiet graphical.target; then exit 1; fi; count=0; test ! -r /run/qemu-hotplug-backup-count || read -r count < /run/qemu-hotplug-backup-count; count=\$\$((count + 1)); printf "%%s\n" "\$\$count" > /run/qemu-hotplug-backup-count; printf "QEMU_HOTPLUG_OK_%%s\n" "\$\$count" > /dev/ttyS0'
EOF_OVERRIDE

install -d -m0755 /etc/systemd/system/btrfs-backup-target@default.service.d
cat > /etc/systemd/system/btrfs-backup-target@default.service.d/qemu-hotplug-test.conf <<'EOF_TARGET_OVERRIDE'
[Service]
ExecStart=
ExecStart=/usr/bin/sh -c 'count=0; test ! -r /run/qemu-hotplug-target-count || read -r count < /run/qemu-hotplug-target-count; count=\$\$((count + 1)); printf "%%s\n" "\$\$count" > /run/qemu-hotplug-target-count; printf "QEMU_TARGET_START_%%s\n" "\$\$count" > /dev/ttyS0'
ExecStop=
ExecStop=/usr/bin/sh -c 'count=0; test ! -r /run/qemu-hotplug-target-count || read -r count < /run/qemu-hotplug-target-count; printf "QEMU_TARGET_STOP_%%s\n" "\$\$count" > /dev/ttyS0'
EOF_TARGET_OVERRIDE

cat > /etc/systemd/system/qemu-hotplug-target-holder.service <<'EOF_HOLDER'
[Unit]
Description=Hold target activation while the QEMU USB device exists
BindsTo=$TARGET_DEVICE_UNIT
After=$TARGET_DEVICE_UNIT btrfs-backup-target@default.service
Requires=btrfs-backup-target@default.service

[Service]
Type=oneshot
ExecStart=/usr/bin/true
RemainAfterExit=yes
EOF_HOLDER
systemctl daemon-reload
udevadm control --reload
! systemd-detect-virt --container >/dev/null 2>&1
! systemctl is-active --quiet graphical.target

udevadm settle --timeout=30
install -d -m0755 /mnt/qemu-provisioning-source
mount /dev/vdc /mnt/qemu-provisioning-source
btrfs subvolume create /mnt/qemu-provisioning-source/home >/dev/null
mount --bind /mnt/qemu-provisioning-source/home /mnt/qemu-provisioning-source/home
install -d -m0700 /mnt/qemu-provisioning-source/.snapshots/home

systemctl start polkit.service btrfs-backupd.service
/run/qemu-test-setup/device-provisioning-client \
    /dev/vdd \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    erase-whole-device \
    qemu-whole-device > /run/qemu-whole-device.json
test "\$(blkid -s PTTYPE -o value /dev/vdd)" = gpt
test -b /dev/vdd1
test "\$(blkid -s TYPE -o value /dev/vdd1)" = crypto_LUKS
grep -Eq '"state"[[:space:]]*:[[:space:]]*"succeeded"' /run/qemu-whole-device.json
test -f /etc/btrfs-backup/profiles/qemu-whole-device/profile.json

sfdisk --dump /dev/vde > /run/qemu-partition-table-before
partition_one_hash="\$(sha256sum /dev/vde1 | awk '{print \$1}')"
/run/qemu-test-setup/device-provisioning-client \
    /dev/vde2 \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    reformat-existing-partition \
    qemu-existing-partition > /run/qemu-existing-partition.json
sfdisk --dump /dev/vde > /run/qemu-partition-table-after
test "\$partition_one_hash" = "\$(sha256sum /dev/vde1 | awk '{print \$1}')"
cmp /run/qemu-partition-table-before /run/qemu-partition-table-after
test "\$(blkid -s TYPE -o value /dev/vde2)" = crypto_LUKS
grep -Eq '"state"[[:space:]]*:[[:space:]]*"succeeded"' /run/qemu-existing-partition.json
test -f /etc/btrfs-backup/profiles/qemu-existing-partition/profile.json

/run/qemu-test-setup/device-provisioning-client \
    /dev/vdf \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    erase-whole-device \
    qemu-manager-kill \
    start-only > /run/qemu-manager-kill.json
manager_operation="\$(operation_id_from /run/qemu-manager-kill.json)"
test -n "\$manager_operation"
manager_unit="btrfs-backup-device-preparation@\$manager_operation.service"
wait_for_helper_pid "\$manager_unit"
systemctl kill --kill-whom=main --signal=STOP "\$manager_unit"
manager_pid="\$(systemctl show --property=MainPID --value btrfs-backupd.service)"
kill -KILL "\$manager_pid"
for _ in \$(seq 1 300); do
    systemctl is-active --quiet btrfs-backupd.service || break
    sleep 0.05
done
systemctl reset-failed btrfs-backupd.service
systemctl start btrfs-backupd.service
systemctl kill --kill-whom=main --signal=CONT "\$manager_unit"
wait_for_transaction_state "\$manager_operation" succeeded
test -f /etc/btrfs-backup/profiles/qemu-manager-kill/profile.json
printf 'QEMU_MANAGER_KILL_OK\n' > /dev/ttyS0

/run/qemu-test-setup/device-provisioning-client \
    /dev/vdg \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    erase-whole-device \
    qemu-helper-kill \
    start-only > /run/qemu-helper-kill.json
helper_operation="\$(operation_id_from /run/qemu-helper-kill.json)"
test -n "\$helper_operation"
helper_unit="btrfs-backup-device-preparation@\$helper_operation.service"
wait_for_helper_pid "\$helper_unit"
systemctl kill --kill-whom=main --signal=STOP "\$helper_unit"
systemctl kill --kill-whom=all --signal=KILL "\$helper_unit"
for _ in \$(seq 1 600); do
    refresh_preparation_status "\$helper_operation" || true
    test "\$(transaction_state "\$helper_operation" 2>/dev/null || true)" = interrupted && break
    sleep 0.1
done
wait_for_transaction_state "\$helper_operation" interrupted
grep -Eq '"errorCode":[[:space:]]*"device-preparation\.helper-exited"' \
    "\$(transaction_file "\$helper_operation")"
printf 'QEMU_HELPER_KILL_OK\n' > /dev/ttyS0

printf 'QEMU_UNPLUG_ATTACH_READY\n' > /dev/ttyS0
for _ in \$(seq 1 600); do
    test -b /dev/vdi && break
    sleep 0.1
done
test -b /dev/vdi
/run/qemu-test-setup/device-provisioning-client \
    /dev/vdi \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    erase-whole-device \
    qemu-device-unplug \
    start-only > /run/qemu-device-unplug.json
unplug_operation="\$(operation_id_from /run/qemu-device-unplug.json)"
test -n "\$unplug_operation"
unplug_unit="btrfs-backup-device-preparation@\$unplug_operation.service"
wait_for_helper_pid "\$unplug_unit"
systemctl kill --kill-whom=main --signal=STOP "\$unplug_unit"
printf 'QEMU_UNPLUG_READY\n' > /dev/ttyS0
for _ in \$(seq 1 600); do
    test ! -b /dev/vdi && break
    sleep 0.1
done
test ! -b /dev/vdi
systemctl kill --kill-whom=main --signal=CONT "\$unplug_unit" || true
for _ in \$(seq 1 600); do
    refresh_preparation_status "\$unplug_operation" || true
    unplug_state="\$(transaction_state "\$unplug_operation" 2>/dev/null || true)"
    if test "\$unplug_state" = failed || test "\$unplug_state" = interrupted; then
        break
    fi
    sleep 0.1
done
grep -Eq '"state":[[:space:]]*"(failed|interrupted)"' "\$(transaction_file "\$unplug_operation")"
grep -Eq '"errorCode":[[:space:]]*"device-preparation\.[^"]+"' "\$(transaction_file "\$unplug_operation")"
printf 'QEMU_REPLACEMENT_ATTACH_READY\n' > /dev/ttyS0
for _ in \$(seq 1 600); do
    test -b /dev/vdi && break
    sleep 0.1
done
test -b /dev/vdi
test "\$(sha256sum /dev/vdi | awk '{print \$1}')" = "$REPLACEMENT_HASH"
printf 'QEMU_UNPLUG_RECOVERY_OK\n' > /dev/ttyS0
printf 'QEMU_PROVISIONING_OK\n' > /dev/ttyS0

/run/qemu-test-setup/device-provisioning-client \
    /dev/vdh \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    erase-whole-device \
    qemu-power-loss \
    start-only > /run/qemu-power-loss.json
power_loss_operation="\$(operation_id_from /run/qemu-power-loss.json)"
test -n "\$power_loss_operation"
power_loss_unit="btrfs-backup-device-preparation@\$power_loss_operation.service"
wait_for_helper_pid "\$power_loss_unit"
for _ in \$(seq 1 1200); do
    test -e /dev/mapper/btrfs-backup-qemu-power-loss && break
    sleep 0.025
done
test -e /dev/mapper/btrfs-backup-qemu-power-loss
systemctl kill --kill-whom=main --signal=STOP "\$power_loss_unit"
printf '%s\n' "\$power_loss_operation" > "\$power_loss_marker"
sync "\$power_loss_marker"
printf 'QEMU_POWER_LOSS_READY\n' > /dev/ttyS0
while :; do sleep 60; done
EOF_SETUP
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
