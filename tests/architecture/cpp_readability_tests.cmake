# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

file(
    GLOB_RECURSE maintained_headers
    RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/apps/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/integrations/kde/*.hpp"
)

set(ownership_violations)
set(utils_violations)
foreach(header IN LISTS maintained_headers)
    file(READ "${PROJECT_SOURCE_DIR}/${header}" content)
    if(content MATCHES "std::unique_ptr<[^>]+>[ \t\r\n]*&")
        list(APPEND ownership_violations "${header}")
    endif()
    if(header MATCHES "(^|/)utils(/|$)" OR header MATCHES "(^|/)Utils\\.hpp$")
        list(APPEND utils_violations "${header}")
    endif()
endforeach()

if(ownership_violations)
    list(JOIN ownership_violations "\n  " violation_list)
    message(FATAL_ERROR "Public ownership must not use std::unique_ptr<T>&:\n  ${violation_list}")
endif()

if(utils_violations)
    list(JOIN utils_violations "\n  " violation_list)
    message(FATAL_ERROR "Use a precise module name instead of utils:\n  ${violation_list}")
endif()
