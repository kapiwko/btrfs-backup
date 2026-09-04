# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED BASH OR BASH STREQUAL "")
    message(FATAL_ERROR "BASH is required")
endif()

file(GLOB_RECURSE scripts
    LIST_DIRECTORIES false
    "${SOURCE_DIR}/*.install"
    "${SOURCE_DIR}/*.sh"
)

if(NOT scripts)
    message(FATAL_ERROR "No Bash files or package install hooks found under ${SOURCE_DIR}")
endif()

foreach(script IN LISTS scripts)
    execute_process(
        COMMAND "${BASH}" -n "${script}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "Bash syntax check failed for ${script}\n"
            "stdout:\n${output}\n"
            "stderr:\n${error}"
        )
    endif()
endforeach()
