# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED BASH OR BASH STREQUAL "")
    message(FATAL_ERROR "BASH is required")
endif()

set(scripts
    "${SOURCE_DIR}/packaging/arch/PKGBUILD.in"
)

foreach(script IN LISTS scripts)
    if(NOT EXISTS "${script}")
        message(FATAL_ERROR "Expected shell source does not exist: ${script}")
    endif()
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
