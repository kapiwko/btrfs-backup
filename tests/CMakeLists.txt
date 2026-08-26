# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(add_btrfsbackup_test target source test_name)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_link_libraries(${target} PRIVATE ${ARGN})
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

add_test(
    NAME dependency-direction
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/dependency_direction_tests.cmake
)

add_subdirectory(unit/backup)
add_subdirectory(unit/config)
add_subdirectory(unit/core)
add_subdirectory(unit/state)
add_subdirectory(unit/platform)
add_subdirectory(unit/cli)
if(BUILD_SYSTEM_MANAGER)
    add_subdirectory(unit/daemon)
endif()
add_subdirectory(integration)
