# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(client IN ITEMS BROWSE_CLIENT PROVISIONING_CLIENT)
    if(NOT DEFINED ${client} OR NOT EXISTS "${${client}}")
        message(FATAL_ERROR "Missing integration client: ${client}")
    endif()
    execute_process(
        COMMAND "${${client}}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 2)
        message(FATAL_ERROR "${client} returned ${result} instead of 2 for invalid arguments")
    endif()
    if(NOT error MATCHES "usage:")
        message(FATAL_ERROR "${client} did not print its usage diagnostic")
    endif()
    if(NOT output STREQUAL "")
        message(FATAL_ERROR "${client} wrote machine output for invalid arguments")
    endif()
endforeach()

execute_process(
    COMMAND "${BROWSE_CLIENT}" default --unknown-mode /tmp
    RESULT_VARIABLE browse_mode_result
    OUTPUT_VARIABLE browse_mode_output
    ERROR_VARIABLE browse_mode_error
)
if(NOT browse_mode_result EQUAL 2 OR NOT browse_mode_error MATCHES "usage:" OR NOT browse_mode_output STREQUAL "")
    message(FATAL_ERROR "Browse client accepted an unknown extended mode")
endif()
