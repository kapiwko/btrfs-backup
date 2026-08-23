cmake_minimum_required(VERSION 3.20)

project(
    btrfs-backup-plasma
    VERSION 2.0.0
    LANGUAGES CXX
)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
set(QT_DEFAULT_MAJOR_VERSION 6)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(CTest)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick)
find_package(KF6 REQUIRED COMPONENTS I18n Package)
find_package(Plasma REQUIRED)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

qt_add_qml_module(btrfsbackup_plasma_backend
    URI org.btrfsbackup.plasma
    VERSION 1.0
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/org/btrfsbackup/plasma
    SOURCES
        src/BackupStatusModel.cpp
        src/BackupStatusModel.hpp
)

target_compile_features(btrfsbackup_plasma_backend PRIVATE cxx_std_20)
target_include_directories(btrfsbackup_plasma_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup_plasma_backend
    PRIVATE
        Qt6::Core
        Qt6::Qml
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup_plasma_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(BTRFSBACKUP_PLASMA_QMLDIR ${KDE_INSTALL_QMLDIR}/org/btrfsbackup/plasma)
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
        ${CMAKE_CURRENT_BINARY_DIR}/org/btrfsbackup/plasma/qmldir
        ${CMAKE_CURRENT_BINARY_DIR}/org/btrfsbackup/plasma/btrfsbackup_plasma_backend.qmltypes
    DESTINATION ${BTRFSBACKUP_PLASMA_QMLDIR}
)

plasma_install_package(package org.btrfsbackup.plasmoid)

if(BUILD_TESTING)
    find_program(
        QMLSCENE_EXECUTABLE
        NAMES qmlscene
        PATHS /usr/lib/qt6/bin
        NO_DEFAULT_PATH
    )
    find_program(QMLSCENE_EXECUTABLE NAMES qmlscene)
    if(QMLSCENE_EXECUTABLE)
        add_test(
            NAME plasma-backend-qml-smoke
            COMMAND
                ${CMAKE_COMMAND} -E env
                QT_QPA_PLATFORM=offscreen
                ${QMLSCENE_EXECUTABLE}
                -I ${CMAKE_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/backend-smoke.qml
        )
    endif()
endif()
