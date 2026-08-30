# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set(namespace_layout
    "src/backup/execution|btrfsbackup::backup::execution"
    "src/backup/planning|btrfsbackup::backup::planning"
    "src/daemon/query|btrfsbackup::daemon::query"
    "src/platform/linux/systemd|btrfsbackup::platform::linux::systemd"
    "src/platform/linux/storage|btrfsbackup::platform::linux::storage"
    "src/platform/linux/filesystem|btrfsbackup::platform::linux::filesystem"
    "src/platform/linux/process|btrfsbackup::platform::linux::process"
    "src/platform/linux/transfer|btrfsbackup::platform::linux::transfer"
    "src/backup/transfer|btrfsbackup::backup::transfer"
    "src/platform/linux|btrfsbackup::platform::linux"
    "src/backup|btrfsbackup::backup"
    "src/config|btrfsbackup::config"
    "src/state|btrfsbackup::state"
    "src/cli|btrfsbackup::cli"
    "src/daemon|btrfsbackup::daemon"
)

file(GLOB_RECURSE namespace_sources
    RELATIVE "${PROJECT_SOURCE_DIR}"
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
foreach(relative IN LISTS namespace_sources)
    if(relative STREQUAL "src/backup/model/Snapshot.hpp"
        OR relative STREQUAL "src/daemon/main.cpp")
        continue()
    endif()
    set(expected_namespace "")
    foreach(mapping IN LISTS namespace_layout)
        string(REPLACE "|" ";" parts "${mapping}")
        list(GET parts 0 prefix)
        list(GET parts 1 candidate_namespace)
        string(FIND "${relative}" "${prefix}/" prefix_position)
        if(prefix_position EQUAL 0)
            set(expected_namespace "${candidate_namespace}")
            break()
        endif()
    endforeach()
    if(expected_namespace STREQUAL "")
        message(FATAL_ERROR "${relative} has no namespace layout mapping")
    endif()
    file(READ "${PROJECT_SOURCE_DIR}/${relative}" content)
    if(NOT content MATCHES "namespace[ \t]+${expected_namespace}[ \t]*\\{")
        message(FATAL_ERROR "${relative} must declare namespace ${expected_namespace}")
    endif()
endforeach()

set(root_namespace_forward_declarations
    "src/backup/execution/actions/BackupRunActionHandler.hpp"
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
