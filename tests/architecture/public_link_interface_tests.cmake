# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(assert_public_link_interface target)
    set(expected ${ARGN})
    get_target_property(interface_libraries ${target} INTERFACE_LINK_LIBRARIES)
    if(NOT interface_libraries)
        set(interface_libraries)
    endif()

    set(actual)
    foreach(library IN LISTS interface_libraries)
        if(NOT library MATCHES "^\\$<LINK_ONLY:")
            list(APPEND actual "${library}")
        endif()
    endforeach()

    list(SORT actual)
    list(SORT expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(
            FATAL_ERROR
            "Unexpected public link interface for ${target}: '${actual}', expected '${expected}'"
        )
    endif()
endfunction()
