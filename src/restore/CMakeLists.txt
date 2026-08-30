# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-restore STATIC
    RepositoryCatalog.cpp
    RepositoryDiscoveryService.cpp
    SnapshotBrowser.cpp
)
target_compile_features(btrfsbackup-restore PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-restore
    PUBLIC_HEADERS
        restore/RepositoryCatalog.hpp
        restore/RepositoryDiscoveryService.hpp
        restore/RestoreError.hpp
        restore/SnapshotBrowser.hpp
)
target_link_libraries(btrfsbackup-restore
    PUBLIC
        btrfsbackup-core
    PRIVATE
        nlohmann_json::nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-restore PRIVATE -Wall -Wextra -Wpedantic)
endif()
