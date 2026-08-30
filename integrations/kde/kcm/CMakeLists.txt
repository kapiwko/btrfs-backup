# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

set(BUILD_SHARED_LIBS ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
kcmutils_add_qml_kcm(
    kcm_btrfsbackup
    SOURCES
        BackupKcm.cpp
        BackupKcm.hpp
)
target_compile_features(kcm_btrfsbackup PRIVATE cxx_std_23)
target_link_libraries(
    kcm_btrfsbackup
    PRIVATE
        Qt6::Core
        Qt6::Gui
        KF6::KCMUtilsQuick
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kcm_btrfsbackup PRIVATE -Wall -Wextra -Wpedantic)
endif()
