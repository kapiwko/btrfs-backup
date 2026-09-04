# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

file(GLOB_RECURSE c_sources RELATIVE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/*.c")
list(FILTER c_sources EXCLUDE REGEX "^(build[^/]*/|\\.git/)")
if(c_sources)
    list(JOIN c_sources "\n  " violations)
    message(FATAL_ERROR "Tracked-style C sources bypass the C++ target graph:\n  ${violations}")
endif()

file(GLOB_RECURSE shell_files RELATIVE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/*.sh")
list(FILTER shell_files EXCLUDE REGEX "^(build[^/]*/|\\.git/)")
# Explicit exception requested for the native QEMU guest bootstrap. It is syntax-tested,
# copied as data, and never builds project code or runs on the host.
set(allowed_shell_files "tests/qemu/hotplug_guest_setup.sh")
foreach(path IN LISTS shell_files)
    if(NOT path IN_LIST allowed_shell_files)
        message(FATAL_ERROR "Unapproved shell file: ${path}")
    endif()
endforeach()
foreach(path IN LISTS allowed_shell_files)
    if(NOT path IN_LIST shell_files)
        message(FATAL_ERROR "Stale shell allowlist entry: ${path}")
    endif()
endforeach()

file(GLOB_RECURSE inspected_files
    "${PROJECT_SOURCE_DIR}/apps/*" "${PROJECT_SOURCE_DIR}/integrations/*"
    "${PROJECT_SOURCE_DIR}/src/*" "${PROJECT_SOURCE_DIR}/tests/*"
    "${PROJECT_SOURCE_DIR}/tools/*"
)
list(FILTER inspected_files INCLUDE REGEX "(CMakeLists\\.txt|\\.(cmake|cpp|hpp|py|service|sh))$")
# These fixtures intentionally exercise process-group termination and pipe failure
# semantics with tiny shell children. They do not orchestrate builds or runtime work.
set(allowed_shell_c_files
    "${PROJECT_SOURCE_DIR}/tests/unit/platform/linux/process/ProcessTests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/unit/platform/linux/transfer/TransferPipelineTests.cpp"
)
foreach(path IN LISTS inspected_files)
    if(IS_DIRECTORY "${path}" OR path STREQUAL CMAKE_CURRENT_LIST_FILE)
        continue()
    endif()
    file(READ "${path}" content LIMIT 2000000)
    if(content MATCHES "shell[ \t]*=[ \t]*True")
        message(FATAL_ERROR "Python shell=True side channel: ${path}")
    endif()
    if(content MATCHES "[\"'](/usr/bin/|/bin/)?(ba)?sh[\"'][ \t]*,[ \t]*[\"']-c" AND
       NOT path IN_LIST allowed_shell_c_files)
        message(FATAL_ERROR "Shell -c side channel: ${path}")
    endif()
endforeach()

file(GLOB_RECURSE test_sources RELATIVE "${PROJECT_SOURCE_DIR}/tests"
    "${PROJECT_SOURCE_DIR}/tests/*.cpp")
file(GLOB_RECURSE test_cmake "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
set(cmake_content)
foreach(path IN LISTS test_cmake)
    file(READ "${path}" fragment)
    string(APPEND cmake_content "\n${fragment}")
endforeach()
foreach(path IN LISTS test_sources)
    get_filename_component(name "${path}" NAME)
    string(FIND "${cmake_content}" "${name}" owner)
    if(owner EQUAL -1)
        message(FATAL_ERROR "Test helper has no CMake target owner: tests/${path}")
    endif()
endforeach()
