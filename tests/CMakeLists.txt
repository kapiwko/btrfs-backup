# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(add_btrfsbackup_test target source test_name)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_link_libraries(${target} PRIVATE ${ARGN})
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

if(BTRFSBACKUP_ARCHITECTURE_TESTS)
include(architecture/public_link_interface_tests.cmake)
include(architecture/model_dependency_tests.cmake)
include(architecture/public_header_compile_tests.cmake)
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
assert_public_link_interface(btrfsbackup-manager-protocol)
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
)
assert_public_link_interface(btrfsbackup-config btrfsbackup-config-domain)
assert_public_link_interface(
    btrfsbackup-platform-linux-config
    btrfsbackup-config
    btrfsbackup-config-ports
)
assert_public_link_interface(
    btrfsbackup-backup-model
    btrfsbackup-config-domain
)
assert_public_link_interface(btrfsbackup-transfer btrfsbackup-core)
assert_public_link_interface(
    btrfsbackup-backup-ports
    btrfsbackup-backup-model
    btrfsbackup-transfer
)
assert_public_link_interface(
    btrfsbackup-backup-testing
    btrfsbackup-backup-ports
)
assert_public_link_interface(
    btrfsbackup-backup-planning
    btrfsbackup-backup-ports
)
assert_public_link_interface(
    btrfsbackup-backup-execution
    btrfsbackup-backup-ports
)
assert_public_link_interface(
    btrfsbackup-backup
    btrfsbackup-backup-execution
    btrfsbackup-config-ports
)
assert_public_link_interface(btrfsbackup-state-persistence-ports)
assert_public_link_interface(btrfsbackup-state-model btrfsbackup-core)
assert_public_link_interface(
    btrfsbackup-state
    btrfsbackup-backup-ports
    btrfsbackup-config-json
    btrfsbackup-state-model
    btrfsbackup-state-persistence-ports
)
assert_public_link_interface(
    btrfsbackup-platform-linux-base
)
assert_public_link_interface(
    btrfsbackup-platform-linux-process
    btrfsbackup-backup-ports
)
assert_public_link_interface(
    btrfsbackup-platform-linux-filesystem
    btrfsbackup-backup-ports
    btrfsbackup-state-persistence-ports
)
assert_public_link_interface(
    btrfsbackup-platform-linux-storage
    btrfsbackup-backup-ports
)
assert_public_link_interface(
    btrfsbackup-platform-linux-transfer
    btrfsbackup-platform-linux-base
    btrfsbackup-platform-linux-process
)
assert_public_link_interface(
    btrfsbackup-platform-linux-systemd
    btrfsbackup-backup-ports
    btrfsbackup-config-ports
)
assert_public_link_interface(
    btrfsbackup-platform-linux
    btrfsbackup-platform-linux-base
    btrfsbackup-platform-linux-filesystem
    btrfsbackup-platform-linux-process
    btrfsbackup-platform-linux-storage
    btrfsbackup-platform-linux-systemd
    btrfsbackup-platform-linux-transfer
)
assert_public_link_interface(
    btrfsbackup-cli
    btrfsbackup-backup
)
if(BUILD_SYSTEM_MANAGER)
    assert_public_link_interface(
        btrfsbackup-daemon-core
        btrfsbackup-config-domain
        btrfsbackup-manager-protocol
    )
    assert_public_link_interface(
        btrfsbackup-daemon-system-control
        btrfsbackup-backup-ports
        btrfsbackup-config-ports
        btrfsbackup-daemon-core
    )
endif()

set(architecture_targets
    btrfsbackup-core
    btrfsbackup-manager-protocol
    btrfsbackup-config-domain
    btrfsbackup-config-json
    btrfsbackup-config-wizard
    btrfsbackup-config-ports
    btrfsbackup-config
    btrfsbackup-platform-linux-config
    btrfsbackup-backup-model
    btrfsbackup-transfer
    btrfsbackup-backup-ports
    btrfsbackup-backup-testing
    btrfsbackup-backup-planning
    btrfsbackup-backup-execution
    btrfsbackup-backup
    btrfsbackup-state-persistence-ports
    btrfsbackup-state-model
    btrfsbackup-state
    btrfsbackup-platform-linux-base
    btrfsbackup-platform-linux-filesystem
    btrfsbackup-platform-linux-process
    btrfsbackup-platform-linux-storage
    btrfsbackup-platform-linux-systemd
    btrfsbackup-platform-linux-transfer
    btrfsbackup-platform-linux
    btrfsbackup-cli
)
if(BUILD_SYSTEM_MANAGER)
    list(APPEND architecture_targets
        btrfsbackup-daemon-core
        btrfsbackup-daemon-system-control
    )
endif()
add_public_header_architecture_tests(${architecture_targets})

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
    NAME cpp-filename-convention
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/cpp_filename_tests.cmake
)

add_test(
    NAME cpp-readability-convention
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/cpp_readability_tests.cmake
)

add_test(
    NAME cpp-special-member-convention
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/cpp_special_member_tests.cmake
)

add_test(
    NAME local-lifecycle-type-convention
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/local_lifecycle_type_tests.cmake
)

add_test(
    NAME config-domain-boundary
    COMMAND
        ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/config_domain_tests.cmake
)
add_test(
    NAME manager-dbus-contract
    COMMAND
        ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/manager_dbus_contract_tests.cmake
)
set_tests_properties(
    dependency-direction
    namespace-layout
    cpp-filename-convention
    cpp-readability-convention
    cpp-special-member-convention
    local-lifecycle-type-convention
    config-domain-boundary
    manager-dbus-contract
    PROPERTIES LABELS architecture
)
endif()

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
