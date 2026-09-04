# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

find_program(GIT_EXECUTABLE git REQUIRED)
if(DEFINED ENV{CLANG_FORMAT} AND NOT "$ENV{CLANG_FORMAT}" STREQUAL "")
    set(clang_format "$ENV{CLANG_FORMAT}")
else()
    find_program(clang_format clang-format REQUIRED)
endif()
find_program(git_clang_format git-clang-format REQUIRED)

if(NOT DEFINED BASE_REF OR BASE_REF STREQUAL "")
    if(DEFINED ENV{CPP_FORMAT_BASE} AND NOT "$ENV{CPP_FORMAT_BASE}" STREQUAL "")
        set(BASE_REF "$ENV{CPP_FORMAT_BASE}")
    elseif(DEFINED ENV{GITHUB_BASE_REF} AND NOT "$ENV{GITHUB_BASE_REF}" STREQUAL "")
        set(BASE_REF "origin/$ENV{GITHUB_BASE_REF}")
    endif()
endif()
if(DEFINED BASE_REF AND BASE_REF MATCHES "^0+$")
    unset(BASE_REF)
endif()

if(DEFINED BASE_REF AND NOT BASE_REF STREQUAL "")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify "${BASE_REF}^{commit}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        OUTPUT_QUIET
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Invalid formatting base ${BASE_REF}: ${error}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" merge-base HEAD "${BASE_REF}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE merge_base
        ERROR_VARIABLE error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot find merge base for ${BASE_REF}: ${error}")
    endif()
    set(BASE_REF "${merge_base}")
elseif(DEFINED ENV{GITHUB_ACTIONS} AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD^
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE parent_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(parent_result EQUAL 0)
        set(BASE_REF HEAD^)
    else()
        set(BASE_REF HEAD)
    endif()
else()
    set(BASE_REF HEAD)
endif()

execute_process(
    COMMAND
        "${GIT_EXECUTABLE}" diff --name-only --diff-filter=A "${BASE_REF}" --
        apps src tests integrations/kde
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE added_output
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot list added C++ files: ${error}")
endif()
string(REPLACE "\n" ";" added_paths "${added_output}")
set(added_cpp_files)
foreach(path IN LISTS added_paths)
    if(path MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$")
        list(APPEND added_cpp_files "${SOURCE_DIR}/${path}")
    endif()
endforeach()
if(added_cpp_files)
    execute_process(
        COMMAND "${clang_format}" --dry-run --Werror --style=file ${added_cpp_files}
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "New C++ files require clang-format changes:\n${error}")
    endif()
endif()

execute_process(
    COMMAND
        "${git_clang_format}"
        --binary "${clang_format}"
        --extensions c,cc,cpp,cxx,h,hh,hpp,hxx
        --style file
        --diff
        "${BASE_REF}"
        --
        apps src tests integrations/kde
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE format_diff
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "git-clang-format failed: ${error}")
endif()
if(format_diff MATCHES "(^|\n)diff --git ")
    message(FATAL_ERROR
        "clang-format changes are required in modified C++ lines:\n${format_diff}\n"
        "Run: git clang-format ${BASE_REF} -- apps src tests integrations/kde"
    )
endif()

message(STATUS "clang-format check passed (base: ${BASE_REF})")
