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
        ManagerErrorMessage.hpp
        ProfileConfigurationModel.cpp
        ProfileConfigurationModel.hpp
        TargetCredentialModel.cpp
        TargetCredentialModel.hpp
        DeviceProvisioningModel.cpp
        DeviceProvisioningModel.hpp
)
set_target_properties(
    kcm_btrfsbackup
    PROPERTIES
        PREFIX ""
        INSTALL_RPATH "$ORIGIN/../../../../qml/org/btrfsbackup/kde"
)
target_compile_features(kcm_btrfsbackup PRIVATE cxx_std_23)
target_link_libraries(
    kcm_btrfsbackup
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::DBus
        KF6::KCMUtilsQuick
        KF6::I18n
        btrfsbackup_kde_desktop_launcher
        btrfsbackup_kde_manager_client
        btrfsbackup_kde_models
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kcm_btrfsbackup PRIVATE -Wall -Wextra -Wpedantic)
endif()
