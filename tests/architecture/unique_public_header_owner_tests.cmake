# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(assert_unique_header_ownership)
    file(
        GLOB_RECURSE source_headers
        RELATIVE "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/src/*.hpp"
    )
    if(NOT BUILD_SYSTEM_MANAGER)
        list(FILTER source_headers EXCLUDE REGEX "^daemon/")
    endif()

    get_property(owned_headers GLOBAL PROPERTY BTRFSBACKUP_OWNED_HEADERS)
    if(NOT owned_headers)
        set(owned_headers)
    endif()

    set(unowned_headers)
    foreach(header IN LISTS source_headers)
        if(NOT header IN_LIST owned_headers)
            list(APPEND unowned_headers "${header}")
        endif()
    endforeach()

    if(unowned_headers)
        list(SORT unowned_headers)
        message(FATAL_ERROR "Headers without a CMake owner: ${unowned_headers}")
    endif()
endfunction()
