# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS CMAKE_COMMAND PYTHON SOURCE_DIR TEST_ROOT)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

set(root "${TEST_ROOT}/release-definitions")
file(REMOVE_RECURSE "${root}")
foreach(target IN ITEMS nix ebuild pkgbuild)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env SOURCE_DATE_EPOCH=1700000000
            "${PYTHON}" "${SOURCE_DIR}/tools/release.py"
            --target "${target}" --dist-dir "${root}/${target}" --skip-tests
        RESULT_VARIABLE result
        ERROR_VARIABLE error
        OUTPUT_QUIET
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${target} release definition failed: ${error}")
    endif()
    file(GLOB archives "${root}/${target}/*-${target}*.tar.gz")
    list(LENGTH archives archive_count)
    if(NOT archive_count EQUAL 1)
        message(FATAL_ERROR "Expected one ${target} definition archive")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar tf "${archives}"
        OUTPUT_VARIABLE entries
        RESULT_VARIABLE list_result
    )
    if(NOT list_result EQUAL 0 OR entries MATCHES "[.]in$")
        message(FATAL_ERROR "Invalid ${target} definition archive")
    endif()
endforeach()

foreach(runner IN ITEMS
        "tests/integration/docker/run_real_btrfs.py"
        "tests/qemu/run_hotplug.py")
    file(READ "${SOURCE_DIR}/${runner}" runner_content)
    string(FIND "${runner_content}" "\"--build-dir\"" build_dir_option)
    string(FIND "${runner_content}" "\"/artifacts/build\"" writable_build_dir)
    if(build_dir_option EQUAL -1 OR writable_build_dir EQUAL -1)
        message(FATAL_ERROR
            "${runner} does not keep container-built packages outside the read-only source mount")
    endif()
endforeach()
