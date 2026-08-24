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

find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLKID REQUIRED IMPORTED_TARGET blkid)
pkg_check_modules(MOUNT REQUIRED IMPORTED_TARGET mount)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(BTRFSUTIL REQUIRED IMPORTED_TARGET libbtrfsutil)

add_subdirectory(cpp/model)
add_subdirectory(cpp/system)
add_subdirectory(cpp/engine)
add_subdirectory(cpp/application)
add_subdirectory(cpp/cli)

if(BUILD_TESTING)
    add_subdirectory(cpp/tests)
endif()
