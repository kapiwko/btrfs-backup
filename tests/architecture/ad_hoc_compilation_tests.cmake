# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

file(
    GLOB_RECURSE c_sources
    RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/apps/*.c"
    "${PROJECT_SOURCE_DIR}/src/*.c"
    "${PROJECT_SOURCE_DIR}/integrations/*.c"
    "${PROJECT_SOURCE_DIR}/tests/*.c"
    "${PROJECT_SOURCE_DIR}/tools/*.c"
)

if(c_sources)
    list(JOIN c_sources "\n  " source_list)
    message(FATAL_ERROR "C helper sources must be migrated to CMake-owned C++ targets:\n  ${source_list}")
endif()

file(
    GLOB_RECURSE orchestration_files
    RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/tests/*.sh"
    "${PROJECT_SOURCE_DIR}/tools/*.sh"
)

set(ad_hoc_compiler_invocations)
foreach(orchestration_file IN LISTS orchestration_files)
    file(READ "${PROJECT_SOURCE_DIR}/${orchestration_file}" content)
    if(content MATCHES "(^|\n)[ \t]*(cc|gcc|clang|c\\+\\+|g\\+\\+)[ \t]+")
        list(APPEND ad_hoc_compiler_invocations "${orchestration_file}")
    endif()
endforeach()

if(ad_hoc_compiler_invocations)
    list(JOIN ad_hoc_compiler_invocations "\n  " invocation_list)
    message(FATAL_ERROR "Build source files through declared CMake targets, not ad hoc compiler commands:\n  ${invocation_list}")
endif()
