# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set(source_roots apps src integrations/kde tests)
set(invalid_files)

foreach(source_root IN LISTS source_roots)
    file(
        GLOB_RECURSE cpp_files
        RELATIVE "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/${source_root}/*.cpp"
        "${PROJECT_SOURCE_DIR}/${source_root}/*.hpp"
    )
    foreach(cpp_file IN LISTS cpp_files)
        get_filename_component(filename "${cpp_file}" NAME)
        if(filename STREQUAL "main.cpp")
            continue()
        endif()
        if(NOT filename MATCHES "^[A-Z][A-Za-z0-9]*\\.(cpp|hpp)$")
            list(APPEND invalid_files "${cpp_file}")
        endif()
    endforeach()
endforeach()

if(invalid_files)
    list(JOIN invalid_files "\n  " invalid_list)
    message(FATAL_ERROR "C++ filenames must use PascalCase:\n  ${invalid_list}")
endif()
