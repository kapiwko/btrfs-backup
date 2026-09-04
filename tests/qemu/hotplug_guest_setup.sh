set -eu
exec 2>/dev/ttyS0
report_failure() {
    status=$?
    if test "$status" -ne 0; then
        printf 'QEMU_SETUP_FAILED status=%s\n' "$status" > /dev/ttyS0
        systemctl status --no-pager --full btrfs-backupd.service > /dev/ttyS0 2>&1 || true
        journalctl --no-pager -b -u btrfs-backupd.service -n 100 > /dev/ttyS0 2>&1 || true
        journalctl --no-pager -b -t btrfs-backup-device-preparation -n 100 > /dev/ttyS0 2>&1 || true
        for transaction in /var/lib/btrfs-backup/device-preparations/*.json; do
            test -f "$transaction" || continue
            printf '%s\n' "--- $transaction ---" > /dev/ttyS0
            operation=${transaction##*/}
            operation=${operation%.json}
            systemctl show "btrfs-backup-device-preparation@$operation.service" \
                -p DevicePolicy -p DeviceAllow -p FragmentPath -p DropInPaths > /dev/ttyS0 2>&1 || true
            cat "$transaction" > /dev/ttyS0
        done
    fi
}
trap report_failure EXIT
tar --zstd -xpf /run/qemu-test-setup/package.pkg.tar.zst -C /
transaction_file() {
    printf '/var/lib/btrfs-backup/device-preparations/%s.json' "$1"
}
transaction_state() {
    sed -n 's/.*"state":[[:space:]]*"\([^"]*\)".*/\1/p' "$(transaction_file "$1")" | head -n1
}
wait_for_transaction_state() {
    operation="$1"
    expected="$2"
    for _ in $(seq 1 1800); do
        test "$(transaction_state "$operation" 2>/dev/null || true)" = "$expected" && return 0
        sleep 0.1
    done
    return 1
}
operation_id_from() {
    sed -n 's/.*"operationId":[[:space:]]*"\([^"]*\)".*/\1/p' "$1" | head -n1
}
wait_for_helper_pid() {
    unit="$1"
    for _ in $(seq 1 600); do
        pid="$(systemctl show --property=MainPID --value "$unit" 2>/dev/null || true)"
        test -n "$pid" && test "$pid" != 0 && return 0
        sleep 0.05
    done
    return 1
}
refresh_preparation_status() {
    busctl --system call io.github.btrfsbackup.Manager1 /io/github/btrfsbackup/Manager1 \
        io.github.btrfsbackup.Manager1 GetDevicePreparation s "$1" >/dev/null
}

power_loss_marker=/var/lib/btrfs-backup/qemu-power-loss-operation
if test -f "$power_loss_marker"; then
    power_loss_operation="$(cat "$power_loss_marker")"
    systemctl start polkit.service btrfs-backupd.service
    wait_for_transaction_state "$power_loss_operation" interrupted
    grep -Eq '"errorCode":[[:space:]]*"device-preparation\.(daemon-restarted|helper-exited)"' \
        "$(transaction_file "$power_loss_operation")"
    for _ in $(seq 1 600); do
        test ! -e /dev/mapper/btrfs-backup-qemu-power-loss && break
        sleep 0.1
    done
    test ! -e /dev/mapper/btrfs-backup-qemu-power-loss
    grep -Eq '"cleanupResult":[[:space:]]*"(mapper-closed|not-required)"' \
        "$(transaction_file "$power_loss_operation")"
    rm -f "$power_loss_marker"
    printf 'QEMU_POWER_LOSS_RECOVERED\n' > /dev/ttyS0
    printf 'QEMU_READY\n' > /dev/ttyS0
    exit 0
fi
install -d -m0755 \
    /etc/btrfs-backup \
    /etc/udev/rules.d \
    /etc/systemd/system/btrfs-backup@default.service.d
cat > /etc/udev/rules.d/99-btrfs-backup-default.rules <<'EOF_RULE'
ACTION=="add", SUBSYSTEM=="block", ENV{ID_FS_TYPE}=="crypto_LUKS", ENV{ID_FS_UUID}=="@TARGET_UUID@", TAG+="systemd", ENV{SYSTEMD_WANTS}+="btrfs-backup@default.service"
EOF_RULE

cat > /usr/local/bin/qemu-hotplug-counter <<'EOF_COUNTER'
#!/usr/bin/bash
set -eu
kind="$1"
action="$2"
if [[ "$kind" == backup ]] && systemctl is-active --quiet graphical.target; then
    exit 1
fi
counter="/run/qemu-hotplug-${kind}-count"
prefix="${kind^^}"
[[ "$kind" != backup ]] || prefix=HOTPLUG_OK
count=0
test ! -r "$counter" || read -r count < "$counter"
if [[ "$action" == start ]]; then
    count=$((count + 1))
    printf '%s\n' "$count" > "$counter"
fi
if [[ "$kind" == backup ]]; then
    printf 'QEMU_%s_%s\n' "$prefix" "$count" > /dev/ttyS0
else
    printf 'QEMU_%s_%s_%s\n' "$prefix" "${action^^}" "$count" > /dev/ttyS0
fi
EOF_COUNTER
chmod 0755 /usr/local/bin/qemu-hotplug-counter

cat > /etc/systemd/system/btrfs-backup@default.service.d/qemu-hotplug-test.conf <<'EOF_OVERRIDE'
[Unit]
OnSuccess=
OnFailure=
Requires=qemu-hotplug-target-holder.service
After=qemu-hotplug-target-holder.service

[Service]
ExecStart=
ExecStart=/usr/local/bin/qemu-hotplug-counter backup start
EOF_OVERRIDE

install -d -m0755 /etc/systemd/system/btrfs-backup-target@default.service.d
cat > /etc/systemd/system/btrfs-backup-target@default.service.d/qemu-hotplug-test.conf <<'EOF_TARGET_OVERRIDE'
[Service]
ExecStart=
ExecStart=/usr/local/bin/qemu-hotplug-counter target start
ExecStop=
ExecStop=/usr/local/bin/qemu-hotplug-counter target stop
EOF_TARGET_OVERRIDE

cat > /etc/systemd/system/qemu-hotplug-target-holder.service <<'EOF_HOLDER'
[Unit]
Description=Hold target activation while the QEMU USB device exists
BindsTo=@TARGET_DEVICE_UNIT@
After=@TARGET_DEVICE_UNIT@ btrfs-backup-target@default.service
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
test "$(blkid -s PTTYPE -o value /dev/vdd)" = gpt
test -b /dev/vdd1
test "$(blkid -s TYPE -o value /dev/vdd1)" = crypto_LUKS
grep -Eq '"state"[[:space:]]*:[[:space:]]*"succeeded"' /run/qemu-whole-device.json
test -f /etc/btrfs-backup/profiles/qemu-whole-device/profile.json

sfdisk --dump /dev/vde > /run/qemu-partition-table-before
partition_one_hash="$(sha256sum /dev/vde1 | awk '{print $1}')"
/run/qemu-test-setup/device-provisioning-client \
    /dev/vde2 \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    reformat-existing-partition \
    qemu-existing-partition > /run/qemu-existing-partition.json
sfdisk --dump /dev/vde > /run/qemu-partition-table-after
test "$partition_one_hash" = "$(sha256sum /dev/vde1 | awk '{print $1}')"
cmp /run/qemu-partition-table-before /run/qemu-partition-table-after
test "$(blkid -s TYPE -o value /dev/vde2)" = crypto_LUKS
grep -Eq '"state"[[:space:]]*:[[:space:]]*"succeeded"' /run/qemu-existing-partition.json
test -f /etc/btrfs-backup/profiles/qemu-existing-partition/profile.json

/run/qemu-test-setup/device-provisioning-client \
    /dev/vdf \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    erase-whole-device \
    qemu-manager-kill \
    start-only > /run/qemu-manager-kill.json
manager_operation="$(operation_id_from /run/qemu-manager-kill.json)"
test -n "$manager_operation"
manager_unit="btrfs-backup-device-preparation@$manager_operation.service"
wait_for_helper_pid "$manager_unit"
systemctl kill --kill-whom=main --signal=STOP "$manager_unit"
manager_pid="$(systemctl show --property=MainPID --value btrfs-backupd.service)"
kill -KILL "$manager_pid"
for _ in $(seq 1 300); do
    systemctl is-active --quiet btrfs-backupd.service || break
    sleep 0.05
done
systemctl reset-failed btrfs-backupd.service
systemctl start btrfs-backupd.service
systemctl kill --kill-whom=main --signal=CONT "$manager_unit"
wait_for_transaction_state "$manager_operation" succeeded
test -f /etc/btrfs-backup/profiles/qemu-manager-kill/profile.json
printf 'QEMU_MANAGER_KILL_OK\n' > /dev/ttyS0

/run/qemu-test-setup/device-provisioning-client \
    /dev/vdg \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    erase-whole-device \
    qemu-helper-kill \
    start-only > /run/qemu-helper-kill.json
helper_operation="$(operation_id_from /run/qemu-helper-kill.json)"
test -n "$helper_operation"
helper_unit="btrfs-backup-device-preparation@$helper_operation.service"
wait_for_helper_pid "$helper_unit"
systemctl kill --kill-whom=main --signal=STOP "$helper_unit"
systemctl kill --kill-whom=all --signal=KILL "$helper_unit"
for _ in $(seq 1 600); do
    refresh_preparation_status "$helper_operation" || true
    test "$(transaction_state "$helper_operation" 2>/dev/null || true)" = interrupted && break
    sleep 0.1
done
wait_for_transaction_state "$helper_operation" interrupted
grep -Eq '"errorCode":[[:space:]]*"device-preparation\.helper-exited"' \
    "$(transaction_file "$helper_operation")"
printf 'QEMU_HELPER_KILL_OK\n' > /dev/ttyS0

printf 'QEMU_UNPLUG_ATTACH_READY\n' > /dev/ttyS0
for _ in $(seq 1 600); do
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
unplug_operation="$(operation_id_from /run/qemu-device-unplug.json)"
test -n "$unplug_operation"
unplug_unit="btrfs-backup-device-preparation@$unplug_operation.service"
wait_for_helper_pid "$unplug_unit"
systemctl kill --kill-whom=main --signal=STOP "$unplug_unit"
printf 'QEMU_UNPLUG_READY\n' > /dev/ttyS0
for _ in $(seq 1 600); do
    test ! -b /dev/vdi && break
    sleep 0.1
done
test ! -b /dev/vdi
systemctl kill --kill-whom=main --signal=CONT "$unplug_unit" || true
for _ in $(seq 1 600); do
    refresh_preparation_status "$unplug_operation" || true
    unplug_state="$(transaction_state "$unplug_operation" 2>/dev/null || true)"
    if test "$unplug_state" = failed || test "$unplug_state" = interrupted; then
        break
    fi
    sleep 0.1
done
grep -Eq '"state":[[:space:]]*"(failed|interrupted)"' "$(transaction_file "$unplug_operation")"
grep -Eq '"errorCode":[[:space:]]*"device-preparation\.[^"]+"' "$(transaction_file "$unplug_operation")"
printf 'QEMU_REPLACEMENT_ATTACH_READY\n' > /dev/ttyS0
for _ in $(seq 1 600); do
    test -b /dev/vdi && break
    sleep 0.1
done
test -b /dev/vdi
test "$(sha256sum /dev/vdi | awk '{print $1}')" = "@REPLACEMENT_HASH@"
printf 'QEMU_UNPLUG_RECOVERY_OK\n' > /dev/ttyS0
printf 'QEMU_PROVISIONING_OK\n' > /dev/ttyS0

/run/qemu-test-setup/device-provisioning-client \
    /dev/vdh \
    /mnt/qemu-provisioning-source/home \
    /run/qemu-test-setup/provisioning.key \
    erase-whole-device \
    qemu-power-loss \
    start-only > /run/qemu-power-loss.json
power_loss_operation="$(operation_id_from /run/qemu-power-loss.json)"
test -n "$power_loss_operation"
power_loss_unit="btrfs-backup-device-preparation@$power_loss_operation.service"
wait_for_helper_pid "$power_loss_unit"
for _ in $(seq 1 1200); do
    test -e /dev/mapper/btrfs-backup-qemu-power-loss && break
    sleep 0.025
done
test -e /dev/mapper/btrfs-backup-qemu-power-loss
systemctl kill --kill-whom=main --signal=STOP "$power_loss_unit"
printf '%s\n' "$power_loss_operation" > "$power_loss_marker"
sync "$power_loss_marker"
printf 'QEMU_POWER_LOSS_READY\n' > /dev/ttyS0
while :; do sleep 60; done
