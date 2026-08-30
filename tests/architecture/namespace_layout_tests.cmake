# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(assert_domain_namespace root expected_namespace)
    file(GLOB_RECURSE sources
        "${PROJECT_SOURCE_DIR}/${root}/*.cpp"
        "${PROJECT_SOURCE_DIR}/${root}/*.hpp"
    )
    foreach(source IN LISTS sources)
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
        if(root STREQUAL "src/backup" AND source MATCHES "/transfer/")
            continue()
        endif()
        if(relative STREQUAL "src/backup/model/Snapshot.hpp"
            OR relative STREQUAL "src/daemon/main.cpp")
            continue()
        endif()

        file(READ "${source}" content)
        if(NOT content MATCHES "namespace[ \t]+${expected_namespace}[ \t]*\\{")
            message(FATAL_ERROR "${relative} must declare namespace ${expected_namespace}")
        endif()
    endforeach()
endfunction()

assert_domain_namespace("src/config" "btrfsbackup::config")
assert_domain_namespace("src/state" "btrfsbackup::state")
assert_domain_namespace("src/backup/transfer" "btrfsbackup::backup::transfer")
assert_domain_namespace("src/backup" "btrfsbackup::backup")
assert_domain_namespace("src/platform/linux" "btrfsbackup::platform::linux")
assert_domain_namespace("src/cli" "btrfsbackup::cli")
assert_domain_namespace("src/daemon" "btrfsbackup::daemon")

set(root_namespace_forward_declarations
    "src/backup/action_handlers/BackupRunActionHandler.hpp"
    "src/backup/ports/Process.hpp"
    "src/cli/BackupTool.hpp"
    "src/state/FilePendingMarkerStore.hpp"
    "src/state/RunHistory.hpp"
)

file(GLOB_RECURSE domain_sources
    "${PROJECT_SOURCE_DIR}/src/backup/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/backup/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/cli/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/cli/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/config/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/daemon/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/daemon/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/platform/linux/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/linux/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/state/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/state/*.hpp"
)
foreach(source IN LISTS domain_sources)
    file(READ "${source}" content)
    if(content MATCHES "namespace[ \t]+btrfsbackup[ \t]*\\{")
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
        if(NOT relative IN_LIST root_namespace_forward_declarations)
            message(FATAL_ERROR "${relative} declares symbols directly in namespace btrfsbackup")
        endif()
    endif()
endforeach()

file(GLOB_RECURSE checked_sources
    "${PROJECT_SOURCE_DIR}/apps/*.cpp"
    "${PROJECT_SOURCE_DIR}/apps/*.hpp"
    "${PROJECT_SOURCE_DIR}/integrations/*.cpp"
    "${PROJECT_SOURCE_DIR}/integrations/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/tests/*.cpp"
    "${PROJECT_SOURCE_DIR}/tests/*.hpp"
)
foreach(source IN LISTS checked_sources)
    file(READ "${source}" content)
    if(content MATCHES "using[ \t]+namespace[ \t]+btrfsbackup")
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
        message(FATAL_ERROR "${relative} uses a global btrfsbackup namespace directive")
    endif()
endforeach()
