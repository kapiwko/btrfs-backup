# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(add_btrfsbackup_test target source test_name)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_link_libraries(${target} PRIVATE ${ARGN})
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

include(architecture/public_link_interface_tests.cmake)
include(architecture/model_dependency_tests.cmake)
include(architecture/unique_public_header_owner_tests.cmake)

assert_unique_header_ownership()

assert_model_dependencies(btrfsbackup-config-domain btrfsbackup-core)
assert_model_dependencies(
    btrfsbackup-backup-model
    btrfsbackup-config-domain
    btrfsbackup-core
)
assert_model_dependencies(btrfsbackup-state-model btrfsbackup-core)
assert_model_dependencies(btrfsbackup-transfer btrfsbackup-core)

assert_public_link_interface(btrfsbackup-core)
assert_public_link_interface(btrfsbackup-config-domain btrfsbackup-core)
assert_public_link_interface(
    btrfsbackup-config-json
    btrfsbackup-config-domain
    nlohmann_json::nlohmann_json
)
assert_public_link_interface(btrfsbackup-config-wizard btrfsbackup-config-domain)
assert_public_link_interface(
    btrfsbackup-config-ports
    btrfsbackup-config-domain
    btrfsbackup-core
)
assert_public_link_interface(btrfsbackup-config btrfsbackup-config-domain)
assert_public_link_interface(
    btrfsbackup-platform-linux-config
    btrfsbackup-config
    btrfsbackup-config-domain
    btrfsbackup-config-json
    btrfsbackup-config-ports
    btrfsbackup-core
)
assert_public_link_interface(
    btrfsbackup-backup-model
    btrfsbackup-config-domain
    btrfsbackup-core
)
assert_public_link_interface(btrfsbackup-transfer btrfsbackup-core)
assert_public_link_interface(
    btrfsbackup-backup-ports
    btrfsbackup-backup-model
    btrfsbackup-config-domain
    btrfsbackup-core
    btrfsbackup-transfer
)
assert_public_link_interface(
    btrfsbackup-backup
    btrfsbackup-backup-model
    btrfsbackup-backup-ports
    btrfsbackup-config-domain
    btrfsbackup-config-ports
    btrfsbackup-core
    btrfsbackup-transfer
)
assert_public_link_interface(btrfsbackup-state-persistence-ports)
assert_public_link_interface(btrfsbackup-state-model btrfsbackup-core)
assert_public_link_interface(
    btrfsbackup-state
    btrfsbackup-backup-model
    btrfsbackup-backup-ports
    btrfsbackup-config-domain
    btrfsbackup-config-json
    btrfsbackup-core
    btrfsbackup-state-model
    btrfsbackup-state-persistence-ports
)
assert_public_link_interface(
    btrfsbackup-platform-linux
    btrfsbackup-backup-ports
    btrfsbackup-config-ports
    btrfsbackup-core
    btrfsbackup-state-persistence-ports
    btrfsbackup-transfer
)
assert_public_link_interface(
    btrfsbackup-cli
    btrfsbackup-backup
    btrfsbackup-backup-model
    btrfsbackup-backup-ports
    btrfsbackup-config-domain
    btrfsbackup-config-ports
    btrfsbackup-core
)
if(BUILD_SYSTEM_MANAGER)
    assert_public_link_interface(
        btrfsbackup-daemon-core
        btrfsbackup-config-domain
        btrfsbackup-core
    )
endif()

add_test(
    NAME dependency-direction
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/dependency_direction_tests.cmake
)

add_test(
    NAME namespace-layout
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/namespace_layout_tests.cmake
)

add_test(
    NAME config-domain-boundary
    COMMAND
        ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/config_domain_tests.cmake
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
