cmake_minimum_required(VERSION 3.20)

project(
    btrfs-backup
    VERSION 1.2.0
    LANGUAGES CXX
)

find_package(nlohmann_json REQUIRED)

add_library(btrfsbackup_profile_tool STATIC
    cpp/src/file_io.cpp
    cpp/src/json_io.cpp
    cpp/src/process.cpp
    cpp/src/profile.cpp
    cpp/src/profile_render.cpp
    cpp/src/profile_tool.cpp
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
