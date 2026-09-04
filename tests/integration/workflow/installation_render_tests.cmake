# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS BACKUPCTL TEST_ROOT)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()
function(run_backupctl)
    execute_process(
        COMMAND "${BACKUPCTL}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        string(JOIN " " arguments ${ARGN})
        message(FATAL_ERROR
            "btrfs-backupctl ${arguments} failed\n"
            "stdout:\n${output}\n"
            "stderr:\n${error}"
        )
    endif()
endfunction()

function(assert_file path)
    if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
        message(FATAL_ERROR "Expected file: ${path}")
    endif()
endfunction()

function(assert_not_exists path)
    if(EXISTS "${path}")
        message(FATAL_ERROR "Path should not exist: ${path}")
    endif()
endfunction()

function(assert_contains path expected)
    assert_file("${path}")
    file(READ "${path}" contents)
    string(FIND "${contents}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing '${expected}' in ${path}")
    endif()
endfunction()

function(assert_not_contains path unexpected)
    assert_file("${path}")
    file(READ "${path}" contents)
    string(FIND "${contents}" "${unexpected}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "Unexpected '${unexpected}' in ${path}")
    endif()
endfunction()

set(output "${TEST_ROOT}/installation-render")
set(profile "${output}/config/profile.json")
file(REMOVE_RECURSE "${output}")
file(MAKE_DIRECTORY
    "${output}/config"
    "${output}/systemd"
    "${output}/udev"
)

run_backupctl(
    profile create
    --output "${profile}"
    --profile laptop
    --name "Laptop backup"
    --device /dev/disk/by-uuid/11111111-2222-3333-4444-555555555555
    --luks-uuid 11111111-2222-3333-4444-555555555555
    --btrfs-uuid 66666666-7777-8888-9999-aaaaaaaaaaaa
    --partition-uuid aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
    --mapper-name backupdisk
    --remote-retention 30
    --local-retention 30
    --minimum-target-free-bytes 5368709120
    --minimum-local-free-bytes 1073741824
    --source root root / /.snapshots/btrfs-backup/root root 30 30
    --source home home /home /.snapshots/btrfs-backup/home home 45 20
)
run_backupctl(
    profile
    --etc-root "${output}/config"
    --udev-root "${output}/udev"
    --systemd-root "${output}/systemd"
    --public-root "${output}/public/profiles"
    save --file "${profile}"
)
run_backupctl(
    installation render
    --file "${profile}"
    --output-dir "${output}"
    --backup-command "${BACKUPCTL} runner execute"
    --eject-script "${BACKUPCTL} target eject"
)
run_backupctl(installation validate --rendered-root "${output}")

assert_file("${output}/config/profile.json")
assert_file("${output}/config/profiles/laptop/profile.json")
assert_contains("${output}/config/profile.json" "\"profileId\": \"laptop\"")
assert_file("${output}/systemd/btrfs-backup.service")
assert_file("${output}/systemd/btrfs-backup@.service")
assert_file("${output}/systemd/btrfs-backup-eject@.service")
assert_file("${output}/systemd/btrfs-backup-target@.service")
assert_file("${output}/systemd/mnt-btrfs\\x2dbackup-laptop.mount")
assert_file("${output}/systemd/btrfs-backup@laptop.service.d/target-mount.conf")
assert_file("${output}/udev/99-btrfs-backup-laptop.rules")
assert_not_exists("${output}/udev/99-btrfs-backup.rules")
assert_not_contains("${output}/udev/99-btrfs-backup-laptop.rules" "ACTION==\"remove\"")
assert_contains("${output}/udev/99-btrfs-backup-laptop.rules" "btrfs-backup@laptop.service")
assert_not_contains("${output}/systemd/btrfs-backup.service" "WantedBy=")
assert_not_contains("${output}/systemd/btrfs-backup.service" "Requires=mnt-btrfs\\x2dbackup-laptop.mount")
assert_contains("${output}/systemd/btrfs-backup.service" "ExecStart=")
assert_contains("${output}/systemd/btrfs-backup.service" "--profile laptop")
assert_contains("${output}/systemd/btrfs-backup.service" "OnSuccess=btrfs-backup-eject@laptop.service")
assert_contains("${output}/systemd/btrfs-backup.service" "OnFailure=btrfs-backup-eject@laptop.service")
assert_contains("${output}/systemd/btrfs-backup@.service" "OnSuccess=btrfs-backup-eject@%i.service")
assert_contains("${output}/systemd/btrfs-backup@.service" "OnFailure=btrfs-backup-eject@%i.service")
assert_contains("${output}/systemd/btrfs-backup-eject@.service" "--from-service --profile %i")
assert_contains("${output}/systemd/btrfs-backup.service" "TimeoutStopSec=90s")
assert_contains("${output}/systemd/btrfs-backup.service" "KillMode=mixed")
assert_contains("${output}/systemd/btrfs-backup.service" "SendSIGKILL=yes")
assert_contains("${output}/systemd/btrfs-backup.service" "NoNewPrivileges=yes")
assert_contains("${output}/systemd/btrfs-backup.service" "PrivateTmp=yes")
assert_contains("${output}/systemd/btrfs-backup.service" "ProtectSystem=full")
assert_contains("${output}/systemd/btrfs-backup.service" "ProtectProc=invisible")
assert_contains("${output}/systemd/btrfs-backup.service" "RestrictAddressFamilies=AF_UNIX AF_NETLINK")
assert_contains("${output}/systemd/btrfs-backup.service" "Environment=PATH=/usr/bin")
assert_contains(
    "${output}/systemd/btrfs-backup@laptop.service.d/target-mount.conf"
    "RequiresMountsFor=\"/mnt/btrfs-backup/laptop\""
)
assert_contains(
    "${output}/systemd/btrfs-backup.service"
    "RequiresMountsFor=\"/mnt/btrfs-backup/laptop\""
)
assert_contains(
    "${output}/systemd/mnt-btrfs\\x2dbackup-laptop.mount"
    "Requires=btrfs-backup-target@laptop.service"
)
assert_contains(
    "${output}/systemd/mnt-btrfs\\x2dbackup-laptop.mount"
    "BindsTo=dev-disk-by\\x2duuid-11111111\\x2d2222\\x2d3333\\x2d4444\\x2d555555555555.device"
)
assert_contains(
    "${output}/systemd/mnt-btrfs\\x2dbackup-laptop.mount"
    "Options=noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd"
)
assert_contains(
    "${output}/systemd/btrfs-backup-target@.service"
    "target activate --from-service --profile %i"
)
assert_contains("${output}/systemd/btrfs-backup-target@.service" "StopWhenUnneeded=yes")
assert_not_exists("${output}/config/fstab.fragment")
assert_not_exists("${output}/config/crypttab.fragment")

file(GLOB_RECURSE rendered_entries LIST_DIRECTORIES false "${output}/*")
foreach(rendered_entry IN LISTS rendered_entries)
    file(READ "${rendered_entry}" contents)
    string(FIND "${contents}" "{{" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "Rendered output contains unresolved placeholders: ${rendered_entry}")
    endif()
endforeach()
