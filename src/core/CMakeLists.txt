# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-core STATIC
    Cancellation.cpp
    ErrorCode.cpp
    Identifiers.cpp
    RuntimeTime.cpp
)

add_library(btrfsbackup-manager-protocol INTERFACE)
target_compile_features(btrfsbackup-manager-protocol INTERFACE cxx_std_23)
target_restricted_include_directories(btrfsbackup-manager-protocol
    INTERFACE_HEADERS
        core/ManagerProtocol.hpp
)

target_compile_features(btrfsbackup-core PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-core
    PUBLIC_HEADERS
        core/Cancellation.hpp
        core/ErrorCode.hpp
        core/Errors.hpp
        core/Identifiers.hpp
        core/RuntimeTime.hpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-core PRIVATE -Wall -Wextra -Wpedantic)
endif()
