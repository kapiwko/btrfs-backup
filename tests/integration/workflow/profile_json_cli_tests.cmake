# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS BACKUPCTL SOURCE_DIR TEST_ROOT)
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

set(work_dir "${TEST_ROOT}/profile-json-cli")
set(rendered "${work_dir}/rendered")
set(saved "${work_dir}/saved")
set(example "${SOURCE_DIR}/data/examples/profile.example.json")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")

run_backupctl(profile validate --file "${example}")
run_backupctl(profile render --file "${example}" --output-dir "${rendered}")

assert_not_exists("${rendered}/etc/btrfs-backup/profiles.d/default.env")
assert_file("${rendered}/etc/btrfs-backup/profiles/default/profile.json")
assert_file("${rendered}/etc/udev/rules.d/99-btrfs-backup-default.rules")
assert_file("${rendered}/etc/systemd/system/btrfs-backup@default.service.d/target-mount.conf")
assert_file("${rendered}/var/lib/btrfs-backup/public/profiles/default.json")
assert_contains("${rendered}/etc/btrfs-backup/profiles/default/profile.json" "\"id\": \"home\"")
assert_contains(
    "${rendered}/etc/udev/rules.d/99-btrfs-backup-default.rules"
    "btrfs-backup@default.service"
)

run_backupctl(
    profile
    --etc-root "${saved}/etc/btrfs-backup"
    --udev-root "${saved}/etc/udev/rules.d"
    --systemd-root "${saved}/etc/systemd/system"
    --public-root "${saved}/var/lib/btrfs-backup/public/profiles"
    save --file "${example}"
)

assert_not_exists("${saved}/etc/btrfs-backup/profiles.d/default.env")
assert_file("${saved}/etc/btrfs-backup/profiles/default/profile.json")
assert_file("${saved}/etc/udev/rules.d/99-btrfs-backup-default.rules")
assert_file("${saved}/etc/systemd/system/btrfs-backup@default.service.d/target-mount.conf")
assert_file("${saved}/var/lib/btrfs-backup/public/profiles/default.json")

execute_process(
    COMMAND "${BACKUPCTL}"
        profile
        --etc-root "${saved}/etc/btrfs-backup"
        show --profile default
    RESULT_VARIABLE show_result
    OUTPUT_FILE "${saved}/show.json"
    ERROR_VARIABLE show_error
)
if(NOT show_result EQUAL 0)
    message(FATAL_ERROR "profile show failed: ${show_error}")
endif()
assert_contains("${saved}/show.json" "\"profileId\": \"default\"")

run_backupctl(
    profile
    --etc-root "${saved}/etc/btrfs-backup"
    export --profile default --output "${saved}/exported.json"
)
assert_file("${saved}/exported.json")
assert_contains("${saved}/exported.json" "\"profileId\": \"default\"")
