# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set(namespace_layout
    "src/backup/execution|btrfsbackup::backup::execution"
    "src/backup/planning|btrfsbackup::backup::planning"
    "src/cli/runner|btrfsbackup::cli::runner"
    "src/cli/profile|btrfsbackup::cli::profile"
    "src/cli/repository|btrfsbackup::cli::repository"
    "src/cli/restore|btrfsbackup::cli::restore"
    "src/cli/status|btrfsbackup::cli::status"
    "src/cli/target|btrfsbackup::cli::target"
    "src/config/json|btrfsbackup::config::json"
    "src/config/wizard|btrfsbackup::config::wizard"
    "src/provisioning|btrfsbackup::provisioning"
    "src/restore|btrfsbackup::restore"
    "src/platform/linux/config|btrfsbackup::platform::linux::config"
    "src/daemon/dbus|btrfsbackup::daemon::dbus"
    "src/daemon/control|btrfsbackup::daemon::control"
    "src/daemon/query|btrfsbackup::daemon::query"
    "src/platform/linux/systemd|btrfsbackup::platform::linux::systemd"
    "src/platform/linux/storage/provisioning|btrfsbackup::platform::linux::storage::provisioning"
    "src/platform/linux/storage|btrfsbackup::platform::linux::storage"
    "src/platform/linux/filesystem|btrfsbackup::platform::linux::filesystem"
    "src/platform/linux/process|btrfsbackup::platform::linux::process"
    "src/platform/linux/restore|btrfsbackup::platform::linux::restore"
    "src/platform/linux/transfer|btrfsbackup::platform::linux::transfer"
    "src/backup/transfer|btrfsbackup::backup::transfer"
    "src/platform/linux|btrfsbackup::platform::linux"
    "src/backup|btrfsbackup::backup"
    "src/config|btrfsbackup::config"
    "src/state|btrfsbackup::state"
    "src/cli|btrfsbackup::cli"
    "src/daemon|btrfsbackup::daemon"
)

function(expected_namespace_for_path relative result)
    set(best_namespace "")
    set(best_prefix_length -1)
    foreach(mapping IN LISTS namespace_layout)
        string(REPLACE "|" ";" parts "${mapping}")
        list(GET parts 0 prefix)
        list(GET parts 1 candidate_namespace)
        string(FIND "${relative}" "${prefix}/" prefix_position)
        string(LENGTH "${prefix}" prefix_length)
        if(prefix_position EQUAL 0 AND prefix_length GREATER best_prefix_length)
            set(best_namespace "${candidate_namespace}")
            set(best_prefix_length "${prefix_length}")
        endif()
    endforeach()
    set("${result}" "${best_namespace}" PARENT_SCOPE)
endfunction()

function(content_matches_namespace relative content result)
    expected_namespace_for_path("${relative}" expected_namespace)
    if(expected_namespace STREQUAL "")
        message(FATAL_ERROR "${relative} has no namespace layout mapping")
    endif()
    if(content MATCHES "namespace[ \t]+${expected_namespace}[ \t]*\\{")
        set("${result}" TRUE PARENT_SCOPE)
    else()
        set("${result}" FALSE PARENT_SCOPE)
    endif()
endfunction()

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
    "${PROJECT_SOURCE_DIR}/src/provisioning/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/provisioning/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/restore/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/restore/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/state/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/state/*.hpp"
)
foreach(relative IN LISTS namespace_sources)
    if(relative STREQUAL "src/backup/model/Snapshot.hpp"
        OR relative STREQUAL "src/daemon/main.cpp")
        continue()
    endif()
    file(READ "${PROJECT_SOURCE_DIR}/${relative}" content)
    content_matches_namespace("${relative}" "${content}" namespace_matches)
    if(NOT namespace_matches)
        expected_namespace_for_path("${relative}" expected_namespace)
        message(FATAL_ERROR "${relative} must declare namespace ${expected_namespace}")
    endif()
endforeach()

set(new_adapter_path "src/platform/linux/config/future/WrongNamespace.cpp")
set(new_adapter_with_wrong_namespace "namespace btrfsbackup::platform::linux {\n}\n")
content_matches_namespace("${new_adapter_path}" "${new_adapter_with_wrong_namespace}" wrong_namespace_accepted)
if(wrong_namespace_accepted)
    message(FATAL_ERROR "namespace layout accepted a new config adapter with the parent Linux namespace")
endif()

set(root_namespace_forward_declarations
    "src/backup/execution/actions/BackupRunActionHandler.hpp"
    "src/backup/ports/Process.hpp"
    "src/cli/BackupTool.hpp"
    "src/state/persistence/FilePendingMarkerStore.hpp"
    "src/state/query/RunHistory.hpp"
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
    "${PROJECT_SOURCE_DIR}/src/provisioning/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/provisioning/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/restore/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/restore/*.hpp"
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
    if(content MATCHES "using[ \t]+namespace[ \t]+[A-Za-z_:][A-Za-z0-9_:]*")
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
        message(FATAL_ERROR "${relative} uses a forbidden namespace directive")
    endif()
endforeach()
