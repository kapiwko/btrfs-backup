# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

# Local resource owners are deliberately exceptional. This inventory makes
# their copy/move/destruction policy visible without pretending to parse C++.
set(local_lifecycle_types
    "integrations/kde/kio/BtrfsBackupWorker.cpp|BrowseSessionPin|immobile"
    "src/backup/execution/LinkedCancellationMonitor.cpp|LinkedCancellationWatch|immobile"
    "src/daemon/control/DevicePreparationUnitController.cpp|SecretBuffer|movable"
    "src/platform/linux/storage/CryptsetupOperations.cpp|SafeSecret|immobile"
    "src/platform/linux/process/ProcessSpawn.cpp|SpawnAttributes|immobile"
    "src/platform/linux/process/ProcessSpawn.cpp|SpawnFileActions|immobile"
    "src/platform/linux/process/TerminationSignalMonitor.cpp|Impl|immobile"
    "src/state/document/BoundedDocumentReader.cpp|FileDescriptor|immobile"
)

file(GLOB_RECURSE implementation_files
    RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/apps/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/integrations/kde/*.cpp"
)

set(observed_types)
set(noexcept_violations)
foreach(source IN LISTS implementation_files)
    file(READ "${PROJECT_SOURCE_DIR}/${source}" content)
    string(REGEX MATCHALL "(class|struct)[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*::[A-Za-z_][A-Za-z0-9_]*" nested_declarations "${content}")
    foreach(declaration IN LISTS nested_declarations)
        string(REGEX REPLACE ".*::([A-Za-z_][A-Za-z0-9_]*)$" "\\1" type_name "${declaration}")
        if(content MATCHES "~${type_name}[ \t]*\\(")
            list(APPEND observed_types "${source}|${type_name}")
            if(NOT content MATCHES "~${type_name}[ \t]*\\([^)]*\\)[^;{]*(noexcept|=[ \t]*default)")
                list(APPEND noexcept_violations "${source}|${type_name}: destructor")
            endif()
        endif()
    endforeach()
    string(REGEX MATCHALL "(class|struct)[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*" declarations "${content}")
    foreach(declaration IN LISTS declarations)
        string(REGEX REPLACE ".*[ \t\r\n]+([A-Za-z_][A-Za-z0-9_]*)$" "\\1" type_name "${declaration}")
        if(content MATCHES "(class|struct)[ \t\r\n]+${type_name}::")
            continue()
        endif()
        set(has_special_member FALSE)
        if(content MATCHES "~${type_name}[ \t]*\\(")
            set(has_special_member TRUE)
            if(NOT content MATCHES "~${type_name}[ \t]*\\([^)]*\\)[^;{]*(noexcept|=[ \t]*default)")
                list(APPEND noexcept_violations "${source}|${type_name}: destructor")
            endif()
        endif()
        if(content MATCHES "${type_name}[ \t]*\\([ \t]*${type_name}[ \t]*&&")
            set(has_special_member TRUE)
            if(NOT content MATCHES "${type_name}[ \t]*\\([ \t]*${type_name}[ \t]*&&[^)]*\\)[^;{]*(noexcept|=[ \t]*delete)")
                list(APPEND noexcept_violations "${source}|${type_name}: move constructor")
            endif()
        endif()
        if(content MATCHES "operator=[ \t]*\\([ \t]*${type_name}[ \t]*&&")
            set(has_special_member TRUE)
            if(NOT content MATCHES "operator=[ \t]*\\([ \t]*${type_name}[ \t]*&&[^)]*\\)[^;{]*(noexcept|=[ \t]*delete)")
                list(APPEND noexcept_violations "${source}|${type_name}: move assignment")
            endif()
        endif()
        if(has_special_member)
            list(APPEND observed_types "${source}|${type_name}")
        endif()
    endforeach()
endforeach()

list(REMOVE_DUPLICATES observed_types)
foreach(observed IN LISTS observed_types)
    set(approved FALSE)
    foreach(entry IN LISTS local_lifecycle_types)
        if(entry MATCHES "^${observed}\\|")
            set(approved TRUE)
            break()
        endif()
    endforeach()
    if(NOT approved)
        list(APPEND unreviewed_types "${observed}")
    endif()
endforeach()

foreach(entry IN LISTS local_lifecycle_types)
    string(REGEX REPLACE "^([^|]+\\|[^|]+)\\|.*$" "\\1" approved_type "${entry}")
    if(NOT approved_type IN_LIST observed_types)
        list(APPEND stale_inventory "${entry}")
    endif()
endforeach()

if(unreviewed_types)
    list(JOIN unreviewed_types "\n  " violations)
    message(FATAL_ERROR "Unreviewed local lifecycle types:\n  ${violations}")
endif()
if(stale_inventory)
    list(JOIN stale_inventory "\n  " violations)
    message(FATAL_ERROR "Stale local lifecycle inventory entries:\n  ${violations}")
endif()
if(noexcept_violations)
    list(REMOVE_DUPLICATES noexcept_violations)
    list(JOIN noexcept_violations "\n  " violations)
    message(FATAL_ERROR "Local move operations and destructors must be noexcept or deleted:\n  ${violations}")
endif()
