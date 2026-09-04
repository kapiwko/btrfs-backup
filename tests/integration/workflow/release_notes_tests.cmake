# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS PYTHON SOURCE_DIR TEST_ROOT)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

function(render_notes output_file)
    execute_process(
        COMMAND "${PYTHON}" "${SOURCE_DIR}/tools/render_release_notes.py" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_FILE "${output_file}"
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        string(JOIN " " arguments ${ARGN})
        message(FATAL_ERROR "Release notes rendering for ${arguments} failed: ${error}")
    endif()
endfunction()

function(assert_contains path expected)
    file(READ "${path}" contents)
    string(FIND "${contents}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing '${expected}' in ${path}")
    endif()
endfunction()

function(assert_not_contains path unexpected)
    file(READ "${path}" contents)
    string(FIND "${contents}" "${unexpected}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "Unexpected '${unexpected}' in ${path}")
    endif()
endfunction()

set(output "${TEST_ROOT}/release-notes")
set(notes "${output}/release-notes.md")
set(initial_notes "${output}/initial-release-notes.md")
file(REMOVE_RECURSE "${output}")
file(MAKE_DIRECTORY "${output}")

render_notes("${notes}" 0.3.3 v0.3.2)
assert_contains("${notes}" "## What's New")
assert_contains("${notes}" "### Target Storage Visibility")
assert_contains("${notes}" "### State API Reliability")
assert_contains("${notes}" "### Plasma Operations")
assert_contains("${notes}" "## Artifacts")
assert_contains("${notes}" "/compare/v0.3.2...v0.3.3")
assert_not_contains("${notes}" "## Unreleased")
assert_not_contains("${notes}" "## 0.3.2")

render_notes("${initial_notes}" 0.1.0)
assert_contains("${initial_notes}" "### Highlights")
assert_contains("${initial_notes}" "/tree/v0.1.0")
assert_not_contains("${initial_notes}" "/compare/")
