# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(add_public_header_architecture_tests)
    set(architecture_targets ${ARGN})
    set(probe_root "${PROJECT_BINARY_DIR}/architecture/header-probes")
    file(MAKE_DIRECTORY "${probe_root}")

    set(probe_targets)
    set(manifest "set(BTRFSBACKUP_ARCHITECTURE_TARGETS \"${architecture_targets}\")\n")
    foreach(target IN LISTS architecture_targets)
        get_target_property(public_headers ${target} BTRFSBACKUP_PUBLIC_HEADERS)
        get_target_property(interface_headers ${target} BTRFSBACKUP_INTERFACE_HEADERS)
        if(NOT public_headers)
            set(public_headers)
        endif()
        if(interface_headers)
            list(APPEND public_headers ${interface_headers})
        endif()

        set(probe_sources)
        set(index 0)
        foreach(header IN LISTS public_headers)
            math(EXPR index "${index} + 1")
            set(source "${probe_root}/${target}/${index}.cpp")
            file(MAKE_DIRECTORY "${probe_root}/${target}")
            file(GENERATE OUTPUT "${source}" CONTENT "#include <${header}>\n")
            list(APPEND probe_sources "${source}")
        endforeach()

        if(probe_sources)
            set(probe_target "${target}-public-header-probe")
            add_library(${probe_target} OBJECT ${probe_sources})
            target_link_libraries(${probe_target} PRIVATE ${target})
            list(APPEND probe_targets ${probe_target})
        endif()

        get_target_property(public_dependencies ${target} INTERFACE_LINK_LIBRARIES)
        if(NOT public_dependencies)
            set(public_dependencies)
        endif()
        set(direct_public_dependencies)
        foreach(dependency IN LISTS public_dependencies)
            if(NOT dependency MATCHES "^\\$<LINK_ONLY:")
                list(APPEND direct_public_dependencies "${dependency}")
            endif()
        endforeach()

        get_target_property(link_dependencies ${target} LINK_LIBRARIES)
        if(NOT link_dependencies)
            set(link_dependencies)
        endif()
        get_target_property(target_type ${target} TYPE)
        string(APPEND manifest "set(BTRFSBACKUP_HEADERS_${target} \"${public_headers}\")\n")
        string(APPEND manifest "set(BTRFSBACKUP_PUBLIC_DEPS_${target} \"${direct_public_dependencies}\")\n")
        string(APPEND manifest "set(BTRFSBACKUP_LINK_DEPS_${target} \"${link_dependencies}\")\n")
        string(APPEND manifest "set(BTRFSBACKUP_TYPE_${target} \"${target_type}\")\n")
    endforeach()

    set(manifest_path "${PROJECT_BINARY_DIR}/architecture/public-header-manifest.cmake")
    file(WRITE "${manifest_path}" "${manifest}")

    add_custom_target(btrfsbackup-public-header-probes DEPENDS ${probe_targets})
    add_test(
        NAME public-header-self-containment
        COMMAND
            ${CMAKE_COMMAND}
            --build ${PROJECT_BINARY_DIR}
            --target btrfsbackup-public-header-probes
            --parallel 8
    )
    add_test(
        NAME cmake-file-api-target-graph
        COMMAND
            ${CMAKE_COMMAND}
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DAPI_BUILD_DIR=${PROJECT_BINARY_DIR}/architecture/file-api-build
            -DBUILD_SYSTEM_MANAGER=${BUILD_SYSTEM_MANAGER}
            -DMANIFEST=${manifest_path}
            -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmake_file_api_tests.cmake
    )

    find_program(CLANG_SCAN_DEPS_EXECUTABLE clang-scan-deps)
    if(CLANG_SCAN_DEPS_EXECUTABLE)
        add_test(
            NAME public-header-dependency-usage
            COMMAND
                ${CMAKE_COMMAND}
                -DSCANNER=${CLANG_SCAN_DEPS_EXECUTABLE}
                -DCOMPILE_COMMANDS=${PROJECT_BINARY_DIR}/compile_commands.json
                -DPROBE_ROOT=${probe_root}
                -DMANIFEST=${manifest_path}
                -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/clang_scan_public_headers.cmake
        )
    else()
        message(STATUS "clang-scan-deps not found: public header dependency usage test disabled")
    endif()
endfunction()
