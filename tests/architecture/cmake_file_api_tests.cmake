# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED API_BUILD_DIR OR NOT DEFINED BUILD_SYSTEM_MANAGER OR NOT DEFINED MANIFEST)
    message(FATAL_ERROR "SOURCE_DIR, API_BUILD_DIR, BUILD_SYSTEM_MANAGER, and MANIFEST are required")
endif()
include("${MANIFEST}")

file(REMOVE_RECURSE "${API_BUILD_DIR}")
file(MAKE_DIRECTORY "${API_BUILD_DIR}/.cmake/api/v1/query")
file(WRITE "${API_BUILD_DIR}/.cmake/api/v1/query/codemodel-v2" "")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -S "${SOURCE_DIR}"
        -B "${API_BUILD_DIR}"
        -DBUILD_TESTING=OFF
        -DBUILD_SYSTEM_MANAGER=${BUILD_SYSTEM_MANAGER}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "File API configure failed:\n${configure_output}\n${configure_error}")
endif()

set(REPLY_DIR "${API_BUILD_DIR}/.cmake/api/v1/reply")

file(GLOB indexes "${REPLY_DIR}/index-*.json")
if(NOT indexes)
    message(FATAL_ERROR "CMake File API did not produce an index in ${REPLY_DIR}")
endif()
list(SORT indexes)
list(POP_BACK indexes index_path)
file(READ "${index_path}" index_json)
string(JSON codemodel_file GET "${index_json}" reply codemodel-v2 jsonFile)
file(READ "${REPLY_DIR}/${codemodel_file}" codemodel_json)
string(JSON target_count LENGTH "${codemodel_json}" configurations 0 targets)

set(target_names)
if(target_count GREATER 0)
    math(EXPR last_target "${target_count} - 1")
    foreach(index RANGE ${last_target})
        string(JSON target_name GET "${codemodel_json}" configurations 0 targets ${index} name)
        string(JSON target_id GET "${codemodel_json}" configurations 0 targets ${index} id)
        string(JSON target_file GET "${codemodel_json}" configurations 0 targets ${index} jsonFile)
        list(APPEND target_names "${target_name}")
        string(MAKE_C_IDENTIFIER "${target_id}" target_id_key)
        set("target_name_${target_id_key}" "${target_name}")
        set("target_file_${target_name}" "${target_file}")
    endforeach()
endif()

foreach(target IN LISTS BTRFSBACKUP_ARCHITECTURE_TARGETS)
    if(BTRFSBACKUP_TYPE_${target} STREQUAL "INTERFACE_LIBRARY")
        continue()
    endif()
    if(NOT target IN_LIST target_names)
        message(FATAL_ERROR "CMake File API codemodel is missing target ${target}")
    endif()

    set(target_file "${target_file_${target}}")
    file(READ "${REPLY_DIR}/${target_file}" target_json)
    string(JSON dependency_count ERROR_VARIABLE dependency_error LENGTH "${target_json}" dependencies)
    set(file_api_dependencies)
    if(NOT dependency_error AND dependency_count GREATER 0)
        math(EXPR last_dependency "${dependency_count} - 1")
        foreach(index RANGE ${last_dependency})
            string(JSON dependency_id GET "${target_json}" dependencies ${index} id)
            string(MAKE_C_IDENTIFIER "${dependency_id}" dependency_id_key)
            if(DEFINED "target_name_${dependency_id_key}")
                list(APPEND file_api_dependencies "${target_name_${dependency_id_key}}")
            endif()
        endforeach()
    endif()

    set(expected_dependencies "${BTRFSBACKUP_LINK_DEPS_${target}}")
    foreach(dependency IN LISTS expected_dependencies)
        if(BTRFSBACKUP_TYPE_${dependency} STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()
        if(dependency MATCHES "^btrfsbackup-" AND NOT dependency IN_LIST file_api_dependencies)
            message(
                FATAL_ERROR
                "CMake File API graph for ${target} is missing direct edge to ${dependency}; "
                "observed: ${file_api_dependencies}"
            )
        endif()
    endforeach()
endforeach()
