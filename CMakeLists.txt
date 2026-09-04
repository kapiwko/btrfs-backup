# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BTRFS_BACKUP_VERSION)
string(STRIP "${BTRFS_BACKUP_VERSION}" BTRFS_BACKUP_VERSION)
if(NOT BTRFS_BACKUP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic version in MAJOR.MINOR.PATCH format")
endif()

project(
    btrfs-backup
    VERSION "${BTRFS_BACKUP_VERSION}"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(GNUInstallDirs)
include(cmake/TargetRestrictedIncludes.cmake)
if(BUILD_TESTING)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()
option(BUILD_KDE_INTEGRATION "Build the optional KDE integration" OFF)
option(BUILD_README_SCREENSHOTS "Build helpers used to render README screenshots" OFF)
option(
    BUILD_SYSTEM_MANAGER
    "Build the privileged system D-Bus manager and device-preparation helper"
    ON
)

add_custom_target(
    check-format
    COMMAND
        ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckCppFormat.cmake
    USES_TERMINAL
)
set(
    BTRFSBACKUP_LOCAL_INSTALL_BUILD_OPTIONS
    ""
    CACHE STRING
    "Semicolon-separated additional build-release options used by the install-local target"
)
add_custom_target(
    install-local
    COMMAND
        ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DDIST_DIR=${PROJECT_SOURCE_DIR}/dist
        "-DLOCAL_INSTALL_BUILD_OPTIONS=${BTRFSBACKUP_LOCAL_INSTALL_BUILD_OPTIONS}"
        -P ${PROJECT_SOURCE_DIR}/cmake/InstallLocalRelease.cmake
    USES_TERMINAL
    VERBATIM
)
if(BUILD_KDE_INTEGRATION)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
option(
    BTRFSBACKUP_ARCHITECTURE_TESTS
    "Enable compile probes and dependency architecture tests"
    ON
)
option(
    BTRFSBACKUP_REAL_RESTORE_TESTS
    "Register the root-only loop-backed real Btrfs restore test"
    OFF
)

find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLKID REQUIRED IMPORTED_TARGET blkid)
pkg_check_modules(CRYPTSETUP REQUIRED IMPORTED_TARGET libcryptsetup)
pkg_check_modules(MOUNT REQUIRED IMPORTED_TARGET mount)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(BTRFSUTIL REQUIRED IMPORTED_TARGET libbtrfsutil)
find_program(
    BTRFSBACKUP_SYSTEMD_CRYPTSETUP_EXECUTABLE
    NAMES systemd-cryptsetup
    PATHS /usr/lib/systemd /lib/systemd /usr/libexec/systemd
)
if(NOT BTRFSBACKUP_SYSTEMD_CRYPTSETUP_EXECUTABLE)
    message(FATAL_ERROR "systemd-cryptsetup executable was not found")
endif()
if(BUILD_SYSTEM_MANAGER)
    pkg_check_modules(FDISK REQUIRED IMPORTED_TARGET fdisk)
    pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
endif()

add_subdirectory(src/core)
add_subdirectory(src/config)
add_subdirectory(src/backup)
add_subdirectory(src/restore)
if(BUILD_SYSTEM_MANAGER)
    add_subdirectory(src/provisioning)
endif()
add_subdirectory(src/platform/linux)
add_subdirectory(src/state)
add_subdirectory(src/cli)
if(BUILD_SYSTEM_MANAGER)
    add_subdirectory(src/daemon)
endif()
add_subdirectory(apps)

if(BUILD_KDE_INTEGRATION)
    add_subdirectory(integrations/kde)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(BTRFSBACKUP_BACKUP_COMMAND "${CMAKE_INSTALL_FULL_BINDIR}/btrfs-backupctl runner execute")
set(BTRFSBACKUP_EJECT_SCRIPT_PATH "${CMAKE_INSTALL_FULL_BINDIR}/btrfs-backupctl target eject")
set(BTRFSBACKUP_TARGET_COMMAND "${CMAKE_INSTALL_FULL_BINDIR}/btrfs-backupctl target")
set(BTRFSBACKUP_MANAGER_EXECUTABLE "${CMAKE_INSTALL_FULL_BINDIR}/btrfs-backupd")
set(BTRFSBACKUP_DEVICE_PREPARATION_EXECUTABLE "${CMAKE_INSTALL_FULL_BINDIR}/btrfs-backup-device-preparation")

configure_file(
    data/systemd/btrfs-backup@.service.example
    btrfs-backup@.service
    @ONLY
)
configure_file(
    data/systemd/btrfs-backup-eject@.service.example
    btrfs-backup-eject@.service
    @ONLY
)
configure_file(
    data/systemd/btrfs-backup-validate@.service.example
    btrfs-backup-validate@.service
    @ONLY
)
configure_file(
    data/systemd/btrfs-backup-target@.service.example
    btrfs-backup-target@.service
    @ONLY
)
configure_file(
    data/systemd/btrfs-backupd.service
    btrfs-backupd.service
    @ONLY
)
configure_file(
    data/systemd/btrfs-backup-device-preparation@.service
    btrfs-backup-device-preparation@.service
    @ONLY
)
configure_file(
    data/dbus/io.github.btrfsbackup.Manager1.service
    io.github.btrfsbackup.Manager1.service
    @ONLY
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/btrfs-backup@.service"
        "${CMAKE_CURRENT_BINARY_DIR}/btrfs-backup-eject@.service"
        "${CMAKE_CURRENT_BINARY_DIR}/btrfs-backup-validate@.service"
        "${CMAKE_CURRENT_BINARY_DIR}/btrfs-backup-target@.service"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/system"
)
if(BUILD_SYSTEM_MANAGER)
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/btrfs-backupd.service"
            "${CMAKE_CURRENT_BINARY_DIR}/btrfs-backup-device-preparation@.service"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/system"
    )
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/io.github.btrfsbackup.Manager1.service"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/system-services"
    )
    install(
        FILES data/dbus/io.github.btrfsbackup.Manager1.conf
        DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/system.d"
    )
    install(
        FILES data/dbus/io.github.btrfsbackup.Manager1.xml
        DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/interfaces"
    )
    install(
        FILES data/polkit/io.github.btrfsbackup.policy
        DESTINATION "${CMAKE_INSTALL_DATADIR}/polkit-1/actions"
    )
endif()
install(
    DIRECTORY data/examples/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/btrfs-backup/examples/config"
)
install(
    FILES data/schemas/profile.schema.json
    DESTINATION "${CMAKE_INSTALL_DATADIR}/btrfs-backup/examples/config"
)
install(
    DIRECTORY data/systemd/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/btrfs-backup/examples/systemd"
)
install(
    DIRECTORY data/udev/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/btrfs-backup/examples/udev"
)
install(
    FILES README.md CHANGELOG.md LICENSE
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(
    DIRECTORY docs/
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    FILES_MATCHING
        PATTERN "*.md"
        PATTERN "*.png"
)
install(CODE
    "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/btrfs-backup/hooks.d\")\n"
    "file(CHMOD \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/btrfs-backup/hooks.d\" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)"
)

include(cmake/ReleasePackaging.cmake)
