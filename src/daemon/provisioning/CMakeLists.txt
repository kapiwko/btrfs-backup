# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-daemon-provisioning STATIC
    DevicePreparationPlan.cpp
    ExistingTargetInspection.cpp
    DevicePreparationPlanBuilder.cpp
    StorageSafetyInspector.cpp
    StorageTopology.cpp
)
target_compile_features(btrfsbackup-daemon-provisioning PUBLIC cxx_std_23)
target_restricted_include_directories(btrfsbackup-daemon-provisioning
    PUBLIC_HEADERS
        daemon/provisioning/DevicePreparationPlan.hpp
        daemon/provisioning/DevicePreparationPlanBuilder.hpp
        daemon/provisioning/ExistingTargetInspection.hpp
        daemon/provisioning/StorageSafetyInspector.hpp
        daemon/provisioning/StorageTopology.hpp
        daemon/provisioning/StorageTopologyReader.hpp
)
target_link_libraries(btrfsbackup-daemon-provisioning
    PRIVATE
        btrfsbackup-core
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-daemon-provisioning PRIVATE -Wall -Wextra -Wpedantic)
endif()
