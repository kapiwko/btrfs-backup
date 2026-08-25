# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" BTRFS_BACKUP_VERSION)
string(STRIP "${BTRFS_BACKUP_VERSION}" BTRFS_BACKUP_VERSION)
if(NOT BTRFS_BACKUP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic version in MAJOR.MINOR.PATCH format")
endif()

project(
    btrfs-backup-plasma
    VERSION "${BTRFS_BACKUP_VERSION}"
    LANGUAGES CXX
)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
set(QT_DEFAULT_MAJOR_VERSION 6)
set(CMAKE_AUTOMOC ON)

include(KDEInstallDirs)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(CTest)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Qml Quick)
find_package(KF6 REQUIRED COMPONENTS I18n Package)
find_package(Plasma REQUIRED)
ki18n_install(plasmoid/po)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

qt_add_qml_module(btrfsbackup_plasma_backend
    URI org.btrfsbackup.plasma
    VERSION 1.0
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/org/btrfsbackup/plasma
    SOURCES
        plasmoid/src/BackupStatusModel.cpp
        plasmoid/src/BackupStatusModel.hpp
)

target_compile_features(btrfsbackup_plasma_backend PRIVATE cxx_std_20)
target_include_directories(btrfsbackup_plasma_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/src)
target_link_libraries(btrfsbackup_plasma_backend
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Qml
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup_plasma_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(
    BTRFSBACKUP_PLASMA_QMLDIR
    "lib/qt6/qml/org/btrfsbackup/plasma"
    CACHE PATH
    "Install path for the btrfs-backup Plasma QML module"
)
set_target_properties(
    btrfsbackup_plasma_backendplugin
    PROPERTIES PREFIX "lib"
)
set_target_properties(
    btrfsbackup_plasma_backend
    btrfsbackup_plasma_backendplugin
    PROPERTIES INSTALL_RPATH "$ORIGIN"
)
install(
    TARGETS
        btrfsbackup_plasma_backend
        btrfsbackup_plasma_backendplugin
    LIBRARY DESTINATION ${BTRFSBACKUP_PLASMA_QMLDIR}
)
install(
    FILES
        ${CMAKE_BINARY_DIR}/org/btrfsbackup/plasma/qmldir
        ${CMAKE_BINARY_DIR}/org/btrfsbackup/plasma/btrfsbackup_plasma_backend.qmltypes
    DESTINATION ${BTRFSBACKUP_PLASMA_QMLDIR}
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/package/metadata.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/metadata.json"
    @ONLY
)
set(
    BTRFSBACKUP_PLASMA_PACKAGE_INSTALL_DIR
    "${KDE_INSTALL_DATADIR}/plasma/plasmoids/org.btrfsbackup.plasmoid"
)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/package/"
    DESTINATION "${BTRFSBACKUP_PLASMA_PACKAGE_INSTALL_DIR}"
    PATTERN "metadata.json.in" EXCLUDE
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/metadata.json"
    DESTINATION "${BTRFSBACKUP_PLASMA_PACKAGE_INSTALL_DIR}"
)

if(BUILD_TESTING)
    find_program(
        QML_EXECUTABLE
        NAMES qml
        PATHS /usr/lib/qt6/bin
        NO_DEFAULT_PATH
    )
    find_program(QML_EXECUTABLE NAMES qml)
    if(QML_EXECUTABLE)
        add_test(
            NAME plasma-backend-qml-smoke
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QML_EXECUTABLE}
                -I ${CMAKE_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/tests/backend-smoke.qml
        )
    endif()
endif()
