# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-daemon-core STATIC
    manager_service.cpp
)
target_compile_features(btrfsbackup-daemon-core PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-daemon-core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-daemon-core
    PUBLIC
        btrfsbackup-config-model
        btrfsbackup-platform-linux
)

add_executable(btrfs-backupd
    dbus_server.cpp
    main.cpp
)
set_target_properties(btrfs-backupd PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_link_libraries(btrfs-backupd
    PRIVATE
        btrfsbackup-daemon-core
        btrfsbackup-config
        PkgConfig::SYSTEMD
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-daemon-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfs-backupd PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS btrfs-backupd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
