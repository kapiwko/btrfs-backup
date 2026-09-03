# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(assert_no_layer_includes layer pattern)
    file(GLOB_RECURSE sources
        "${PROJECT_SOURCE_DIR}/src/${layer}/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/${layer}/*.hpp"
    )
    foreach(source IN LISTS sources)
        file(READ "${source}" content)
        if(content MATCHES "#include[ \t]+[<\"](${pattern})/")
            file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
            message(FATAL_ERROR "Forbidden dependency in ${relative}: ${CMAKE_MATCH_1}")
        endif()
    endforeach()
endfunction()

assert_no_layer_includes("core" "config|backup|state|platform|cli|daemon")
assert_no_layer_includes("config" "backup|state|platform|cli|daemon")
assert_no_layer_includes("backup" "state|platform|cli|daemon")
assert_no_layer_includes("backup/model" "backup/ports|state|platform|cli|daemon")
assert_no_layer_includes("state" "platform|cli|daemon")
assert_no_layer_includes("provisioning" "config|backup|restore|state|platform|cli|daemon")
assert_no_layer_includes("platform/linux/storage/provisioning" "cli|daemon")

file(READ "${PROJECT_SOURCE_DIR}/src/backup/BackupService.hpp" backup_service_header)
if(backup_service_header MATCHES "CancellationToken[ \t]*&")
    message(FATAL_ERROR "BackupService must not retain a cross-run cancellation token")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/backup/execution/RunExecutionContext.hpp" run_execution_context_header)
if(NOT run_execution_context_header MATCHES "CancellationToken[ \t\r\n]+cancellation")
    message(FATAL_ERROR "RunExecutionContext must own the run cancellation token")
endif()

file(GLOB_RECURSE production_sources
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
)
foreach(source IN LISTS production_sources)
    file(READ "${source}" content)
    if(content MATCHES "IRunStateRepository")
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
        message(FATAL_ERROR "Broad run state persistence port reintroduced in ${relative}")
    endif()
endforeach()
