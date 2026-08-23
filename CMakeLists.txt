cmake_minimum_required(VERSION 3.20)

project(
    btrfs-backup
    VERSION 1.2.0
    LANGUAGES CXX
)

include(CTest)

find_package(nlohmann_json REQUIRED)

add_library(btrfsbackup_profile_tool STATIC
    cpp/src/ctl_tool.cpp
    cpp/src/file_io.cpp
    cpp/src/history.cpp
    cpp/src/json_io.cpp
    cpp/src/process.cpp
    cpp/src/profile.cpp
    cpp/src/profile_compose.cpp
    cpp/src/profile_list.cpp
    cpp/src/profile_render.cpp
    cpp/src/profile_store.cpp
    cpp/src/profile_tool.cpp
    cpp/src/status.cpp
)

target_compile_features(btrfsbackup_profile_tool PUBLIC cxx_std_20)
target_include_directories(btrfsbackup_profile_tool
    PUBLIC
        cpp/include
)
target_link_libraries(btrfsbackup_profile_tool
    PUBLIC
        nlohmann_json::nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup_profile_tool PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(btrfs-backup-profile
    cpp/apps/btrfs-backup-profile.cpp
)

target_link_libraries(btrfs-backup-profile
    PRIVATE
        btrfsbackup_profile_tool
)

add_executable(btrfs-backupctl
    cpp/apps/btrfs-backupctl.cpp
)

target_link_libraries(btrfs-backupctl
    PRIVATE
        btrfsbackup_profile_tool
)

if(BUILD_TESTING)
    add_executable(btrfsbackup-profile-tests
        cpp/tests/profile_tests.cpp
    )

    add_executable(btrfsbackup-ctl-tests
        cpp/tests/ctl_tests.cpp
    )

    target_link_libraries(btrfsbackup-profile-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    target_link_libraries(btrfsbackup-ctl-tests
        PRIVATE
            btrfsbackup_profile_tool
    )

    add_test(NAME profile-tests COMMAND btrfsbackup-profile-tests)
    add_test(NAME ctl-tests COMMAND btrfsbackup-ctl-tests)
endif()
