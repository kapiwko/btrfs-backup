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

include(CTest)
option(BUILD_KDE_INTEGRATION "Build the optional KDE integration" OFF)

find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLKID REQUIRED IMPORTED_TARGET blkid)
pkg_check_modules(MOUNT REQUIRED IMPORTED_TARGET mount)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(BTRFSUTIL REQUIRED IMPORTED_TARGET libbtrfsutil)

add_subdirectory(src/config)
add_subdirectory(src/backup)
add_subdirectory(src/platform/linux)
add_subdirectory(src/state)
add_subdirectory(src/cli)
add_subdirectory(apps)

if(BUILD_KDE_INTEGRATION)
    add_subdirectory(integrations/kde)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
