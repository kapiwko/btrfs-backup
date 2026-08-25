# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_btrfsbackup_test(
    btrfsbackup-profile-schema-contract-tests
    profile_schema_contract_tests.cpp
    profile-schema-contract-tests
    btrfsbackup-config-model
)

target_compile_definitions(btrfsbackup-profile-schema-contract-tests
    PRIVATE BTRFSBACKUP_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

add_test(
    NAME cmake-install-tests
    COMMAND
        ${CMAKE_COMMAND}
        -DBUILD_DIR=${PROJECT_BINARY_DIR}
        -DINSTALL_ROOT=${PROJECT_BINARY_DIR}/cmake-install-test
        -DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_install_tests.cmake
)
