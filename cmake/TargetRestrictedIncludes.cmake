# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(target_restricted_include_directories target)
    cmake_parse_arguments(ARG "" "" "PUBLIC_HEADERS;PRIVATE_HEADERS;INTERFACE_HEADERS" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unexpected arguments for ${target}: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    foreach(visibility PUBLIC PRIVATE INTERFACE)
        set(headers "${ARG_${visibility}_HEADERS}")
        if(NOT headers)
            continue()
        endif()

        string(TOLOWER "${visibility}" visibility_directory)
        set(include_root "${CMAKE_CURRENT_BINARY_DIR}/target-includes/${target}/${visibility_directory}")
        file(REMOVE_RECURSE "${include_root}")

        foreach(header IN LISTS headers)
            set(source "${PROJECT_SOURCE_DIR}/src/${header}")
            set(destination "${include_root}/${header}")
            if(NOT EXISTS "${source}")
                message(FATAL_ERROR "Header declared by ${target} does not exist: ${source}")
            endif()

            string(MD5 header_key "${header}")
            get_property(
                existing_owner
                GLOBAL
                PROPERTY BTRFSBACKUP_HEADER_OWNER_${header_key}
            )
            if(existing_owner)
                message(
                    FATAL_ERROR
                    "Header ${header} is owned by both ${existing_owner} and ${target}"
                )
            endif()
            set_property(
                GLOBAL
                PROPERTY BTRFSBACKUP_HEADER_OWNER_${header_key} ${target}
            )
            set_property(GLOBAL APPEND PROPERTY BTRFSBACKUP_OWNED_HEADERS ${header})

            get_filename_component(destination_directory "${destination}" DIRECTORY)
            file(MAKE_DIRECTORY "${destination_directory}")
            file(CREATE_LINK "${source}" "${destination}" SYMBOLIC RESULT link_result)
            if(NOT link_result STREQUAL "0")
                message(FATAL_ERROR "Cannot create include view for ${target}: ${link_result}")
            endif()
        endforeach()

        set_property(
            TARGET ${target}
            APPEND
            PROPERTY BTRFSBACKUP_${visibility}_HEADERS ${headers}
        )

        target_include_directories(${target} ${visibility} "${include_root}")
    endforeach()
endfunction()
