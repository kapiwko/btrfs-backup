# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS BACKUPCTL SOURCE_DIR TEST_ROOT)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

function(capture_help output_file)
    execute_process(
        COMMAND "${BACKUPCTL}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_FILE "${output_file}"
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        string(JOIN " " arguments ${ARGN})
        message(FATAL_ERROR "btrfs-backupctl ${arguments} failed: ${error}")
    endif()
endfunction()

function(assert_contains path expected)
    file(READ "${path}" contents)
    string(FIND "${contents}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing '${expected}' in ${path}")
    endif()
endfunction()

function(assert_not_contains path unexpected)
    file(READ "${path}" contents)
    string(FIND "${contents}" "${unexpected}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "Unexpected '${unexpected}' in ${path}")
    endif()
endfunction()

function(assert_command_rejected)
    execute_process(
        COMMAND "${BACKUPCTL}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(result EQUAL 0)
        string(JOIN " " arguments ${ARGN})
        message(FATAL_ERROR "Removed or unknown command is still accepted: ${arguments}")
    endif()
endfunction()

set(output "${TEST_ROOT}/command-surface")
file(REMOVE_RECURSE "${output}")
file(MAKE_DIRECTORY "${output}")

capture_help("${output}/root.txt" --help)
capture_help("${output}/profile.txt" profile --help)
capture_help("${output}/status.txt" status --help)

assert_not_contains("${output}/root.txt" "state COMMAND")
assert_not_contains("${output}/profile.txt" "sources --file")
assert_contains("${output}/profile.txt" "regenerate --all")
assert_not_contains("${output}/status.txt" "write [OPTIONS]")
assert_not_contains("${output}/status.txt" "--json")
assert_contains(
    "${SOURCE_DIR}/packaging/arch/btrfs-backup.install"
    "btrfs-backupctl profile regenerate --all"
)

assert_command_rejected(state --help)
assert_command_rejected(profile sources --help)
assert_command_rejected(profile migrate --help)
assert_command_rejected(status write --help)
