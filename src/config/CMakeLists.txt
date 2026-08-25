# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-identifier-model STATIC
    identifiers.cpp
)

target_compile_features(btrfsbackup-identifier-model PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-identifier-model PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(btrfsbackup-config-model STATIC
    json_io.cpp
    profile.cpp
    validation.cpp
    wizard/profile_wizard_model.cpp
)

target_compile_features(btrfsbackup-config-model PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-config-model PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-config-model PUBLIC btrfsbackup-identifier-model nlohmann_json::nlohmann_json)

add_library(btrfsbackup-config STATIC
    application_paths.cpp
    installation_render.cpp
    installation_service.cpp
    installation_validate.cpp
    profile_loader.cpp
    profile_render.cpp
    render_directory.cpp
    profile_service.cpp
    profile_store.cpp
    wizard/profile_wizard.cpp
    wizard/profile_wizard_device.cpp
    wizard/profile_wizard_install.cpp
    wizard/profile_wizard_paths.cpp
    wizard/profile_wizard_prompt.cpp
    wizard/profile_wizard_sources.cpp
)

target_compile_features(btrfsbackup-config PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-config PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(btrfsbackup-config PUBLIC btrfsbackup-config-model btrfsbackup-platform-linux)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-identifier-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config-model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config PRIVATE -Wall -Wextra -Wpedantic)
endif()
