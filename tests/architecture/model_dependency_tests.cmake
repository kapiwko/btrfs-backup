# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(assert_model_dependencies target)
    set(expected_dependencies ${ARGN})
    get_target_property(actual_dependencies ${target} LINK_LIBRARIES)
    if(NOT actual_dependencies)
        set(actual_dependencies)
    endif()

    list(SORT actual_dependencies)
    list(SORT expected_dependencies)
    if(NOT "${actual_dependencies}" STREQUAL "${expected_dependencies}")
        message(
            FATAL_ERROR
            "Unexpected dependencies for model target ${target}: "
            "'${actual_dependencies}', expected '${expected_dependencies}'"
        )
    endif()

    get_target_property(target_sources ${target} SOURCES)
    get_target_property(target_source_directory ${target} SOURCE_DIR)
    get_target_property(public_headers ${target} BTRFSBACKUP_PUBLIC_HEADERS)
    if(NOT public_headers)
        set(public_headers)
    endif()

    set(owned_files)
    foreach(source IN LISTS target_sources)
        if(IS_ABSOLUTE "${source}")
            list(APPEND owned_files "${source}")
        else()
            list(APPEND owned_files "${target_source_directory}/${source}")
        endif()
    endforeach()
    foreach(header IN LISTS public_headers)
        list(APPEND owned_files "${PROJECT_SOURCE_DIR}/src/${header}")
    endforeach()

    string(CONCAT forbidden_include_pattern
        "#include[ \t]+[<\"]"
        "(config/model/(json|json_io|profile_document)\\.hpp|"
        "platform/|daemon/|cli/|nlohmann/|dbus/|systemd/|"
        "Qt|Q[A-Z]|K[A-Z]|linux/|sys/|unistd\\.h|fcntl\\.h|"
        "libudev|libmount|blkid|btrfs|libcryptsetup|cryptsetup\\.h)"
    )
    foreach(owned_file IN LISTS owned_files)
        file(READ "${owned_file}" content)
        if(content MATCHES "${forbidden_include_pattern}")
            file(RELATIVE_PATH relative_path "${PROJECT_SOURCE_DIR}" "${owned_file}")
            message(
                FATAL_ERROR
                "Model target ${target} includes forbidden dependency in ${relative_path}: "
                "${CMAKE_MATCH_1}"
            )
        endif()
    endforeach()
endfunction()
