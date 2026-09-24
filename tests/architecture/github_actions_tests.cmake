# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

file(GLOB workflow_files "${PROJECT_SOURCE_DIR}/.github/workflows/*.yml")
set(codeql_pins)
foreach(workflow IN LISTS workflow_files)
    file(READ "${workflow}" content)
    string(REGEX MATCHALL "github/codeql-action/(init|analyze|upload-sarif)@[^ \t\r\n]+" uses "${content}")
    foreach(use IN LISTS uses)
        string(REGEX REPLACE "^.*@" "" pin "${use}")
        list(APPEND codeql_pins "${pin}")
    endforeach()
endforeach()

list(REMOVE_DUPLICATES codeql_pins)
list(LENGTH codeql_pins pin_count)
if(pin_count GREATER 1)
    list(JOIN codeql_pins ", " pins)
    message(FATAL_ERROR "github/codeql-action steps use mixed pins: ${pins}")
endif()
