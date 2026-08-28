# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-daemon-core STATIC
    device_state_query_service.cpp
    history_query_service.cpp
    manager_document_reader.cpp
    manager_service.cpp
    profile_query_service.cpp
    status_query_service.cpp
)
target_compile_features(btrfsbackup-daemon-core PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-daemon-core
    PUBLIC_HEADERS
        daemon/device_state_query_service.hpp
        daemon/history_query_service.hpp
        daemon/manager_paths.hpp
        daemon/manager_response_models.hpp
        daemon/manager_service.hpp
        daemon/profile_query_service.hpp
        daemon/status_query_service.hpp
    PRIVATE_HEADERS
        daemon/manager_document_reader.hpp
)
target_link_libraries(btrfsbackup-daemon-core
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-config-domain
        btrfsbackup-platform-linux
)

add_executable(btrfs-backupd
    dbus_server.cpp
    main.cpp
)
target_restricted_include_directories(btrfs-backupd
    PRIVATE_HEADERS
        daemon/dbus_server.hpp
)
set_target_properties(btrfs-backupd PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_link_libraries(btrfs-backupd
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-daemon-core
        btrfsbackup-platform-linux-config
        PkgConfig::SYSTEMD
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-daemon-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfs-backupd PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS btrfs-backupd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
