# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

file(
    GLOB_RECURSE maintained_headers
    RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/apps/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/integrations/kde/*.hpp"
)

# Explicit special members are intentional exceptions to the rule of zero.
# Keep this inventory narrow: adding a new owner or immobile type requires a
# review of its lifecycle before the architecture test accepts it.
set(approved_special_member_types
    "src/backup/RunExecutionContext.hpp:RunExecutionContext"
    "src/backup/action_handlers/RecoveryActionHandler.hpp:RecoveryActionHandler"
    "src/backup/action_handlers/RetentionActionHandler.hpp:RetentionActionHandler"
    "src/backup/action_handlers/SnapshotActionHandler.hpp:SnapshotActionHandler"
    "src/backup/transfer/ThreadedAsyncTransferHandle.hpp:ThreadedAsyncTransferHandle"
    "src/cli/BackupTool.hpp:TerminationSignalMonitor"
    "src/cli/RunnerComposition.hpp:RunnerComposition"
    "src/config/ports/IProfileRepository.hpp:IProfileRepository"
    "src/core/Cancellation.hpp:CancellationToken"
    "src/daemon/ManagerAuditLog.hpp:FileManagerAuditLog"
    "src/daemon/ManagerChangeMonitor.hpp:ManagerChangeMonitor"
    "src/daemon/ManagerService.hpp:ManagerService"
    "src/daemon/OperationEnvironmentFile.hpp:OperationEnvironmentFile"
    "src/platform/linux/ChildProcess.hpp:ChildProcess"
    "src/platform/linux/FileLock.hpp:FileLock"
    "src/platform/linux/OwnedFileDescriptor.hpp:OwnedFileDescriptor"
    "src/platform/linux/PosixCancellationSignal.hpp:PosixCancellationSignal"
    "src/platform/linux/SafeDirectoryRoot.hpp:SafeDirectoryHandle"
    "src/platform/linux/SafeDirectoryRoot.hpp:SafeDirectoryRoot"
    "src/platform/linux/SystemdMountedTargetSession.hpp:SystemdMountedTargetSession"
    "src/platform/linux/TerminationSignalMonitor.hpp:TerminationSignalMonitor"
    "src/state/FileActiveRunRegistration.hpp:FileActiveRunRegistration"
    "src/state/PollingCancellationWatch.hpp:PollingCancellationWatch"
)

set(observed_special_member_types)
set(unapproved_special_member_types)
set(noexcept_violations)

foreach(header IN LISTS maintained_headers)
    file(STRINGS "${PROJECT_SOURCE_DIR}/${header}" lines)
    foreach(line IN LISTS lines)
        set(type_name "")
        set(is_move FALSE)
        set(is_destructor FALSE)

        if(line MATCHES "~([A-Za-z_][A-Za-z0-9_]*)[ \\t]*\\(")
            set(type_name "${CMAKE_MATCH_1}")
            if(line MATCHES "=[ \\t]*default" AND line MATCHES "virtual|override")
                continue()
            endif()
            set(is_destructor TRUE)
        elseif(line MATCHES "operator=[ \\t]*\\([ \\t]*(const[ \\t]+)?([A-Za-z_][A-Za-z0-9_]*)[ \\t]*(&&|&)")
            set(type_name "${CMAKE_MATCH_2}")
            if(CMAKE_MATCH_3 STREQUAL "&&")
                set(is_move TRUE)
            endif()
        elseif(line MATCHES "([A-Za-z_][A-Za-z0-9_]*)[ \\t]*\\([ \\t]*(const[ \\t]+)?([A-Za-z_][A-Za-z0-9_]*)[ \\t]*(&&|&)")
            if(CMAKE_MATCH_1 STREQUAL CMAKE_MATCH_3)
                set(type_name "${CMAKE_MATCH_1}")
                if(CMAKE_MATCH_4 STREQUAL "&&")
                    set(is_move TRUE)
                endif()
            endif()
        endif()

        if(type_name STREQUAL "")
            continue()
        endif()

        set(type_key "${header}:${type_name}")
        list(APPEND observed_special_member_types "${type_key}")
        if(NOT type_key IN_LIST approved_special_member_types)
            list(APPEND unapproved_special_member_types "${type_key}")
        endif()

        if(is_move AND NOT line MATCHES "=[ \\t]*delete" AND NOT line MATCHES "noexcept")
            list(APPEND noexcept_violations "${header}: ${line}")
        endif()
        if(is_destructor AND NOT line MATCHES "=[ \\t]*default" AND NOT line MATCHES "noexcept")
            list(APPEND noexcept_violations "${header}: ${line}")
        endif()
    endforeach()
endforeach()

list(REMOVE_DUPLICATES observed_special_member_types)
list(SORT observed_special_member_types)
list(JOIN observed_special_member_types "\n  " observed_list)
message(STATUS "Explicit special-member inventory:\n  ${observed_list}")

if(unapproved_special_member_types)
    list(REMOVE_DUPLICATES unapproved_special_member_types)
    list(JOIN unapproved_special_member_types "\n  " violation_list)
    message(FATAL_ERROR "Unreviewed explicit special-member declarations:\n  ${violation_list}")
endif()

if(noexcept_violations)
    list(JOIN noexcept_violations "\n  " violation_list)
    message(FATAL_ERROR "Move operations and non-default destructors must be noexcept:\n  ${violation_list}")
endif()
