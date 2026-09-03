# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-provisioning STATIC
    ConfiguredBackupTargetMarker.cpp
    DevicePreparationPlan.cpp
    ExistingTargetInspection.cpp
    DevicePreparationPlanBuilder.cpp
    StorageSafetyInspector.cpp
    StorageTopology.cpp
)
target_compile_features(btrfsbackup-provisioning PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-provisioning
    PUBLIC_HEADERS
        provisioning/ConfiguredBackupTargetMarker.hpp
        provisioning/DevicePreparation.hpp
        provisioning/DevicePreparationPlan.hpp
        provisioning/DevicePreparationPlanBuilder.hpp
        provisioning/DevicePreparationTransaction.hpp
        provisioning/ExistingTargetInspection.hpp
        provisioning/StorageSafetyInspector.hpp
        provisioning/StorageTopology.hpp
        provisioning/StorageTopologyReader.hpp
)
target_link_libraries(btrfsbackup-provisioning
    PRIVATE
        btrfsbackup-core
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-provisioning PRIVATE -Wall -Wextra -Wpedantic)
endif()
