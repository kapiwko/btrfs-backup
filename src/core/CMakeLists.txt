# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-core STATIC
    cancellation.cpp
    error_code.cpp
)

target_compile_features(btrfsbackup-core PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-core
    PUBLIC_HEADERS
        core/cancellation.hpp
        core/error_code.hpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-core PRIVATE -Wall -Wextra -Wpedantic)
endif()
