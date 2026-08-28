# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-config-domain STATIC
    model/validation.cpp
)

target_compile_features(btrfsbackup-config-domain PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config-domain
    PUBLIC_HEADERS
        config/model/profile.hpp
        config/model/validation.hpp
        config/application_paths.hpp
)
target_link_libraries(btrfsbackup-config-domain PUBLIC btrfsbackup-core)

add_library(btrfsbackup-config-json STATIC
    model/json_io.cpp
    model/profile_document.cpp
)

target_compile_features(btrfsbackup-config-json PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config-json
    PUBLIC_HEADERS
        config/model/json.hpp
        config/model/json_io.hpp
        config/model/profile_document.hpp
)
target_link_libraries(btrfsbackup-config-json PUBLIC btrfsbackup-config-domain nlohmann_json::nlohmann_json)

add_library(btrfsbackup-config-wizard STATIC
    wizard/profile_wizard_model.cpp
)

target_compile_features(btrfsbackup-config-wizard PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config-wizard
    PUBLIC_HEADERS
        config/wizard/profile_wizard_model.hpp
)
target_link_libraries(btrfsbackup-config-wizard PUBLIC btrfsbackup-config-domain PRIVATE btrfsbackup-config-json)

add_library(btrfsbackup-config-ports STATIC
    ports/profile_repository.cpp
)

target_compile_features(btrfsbackup-config-ports PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config-ports
    PUBLIC_HEADERS
        config/ports/configuration_activator.hpp
        config/ports/profile_repository.hpp
)
target_link_libraries(btrfsbackup-config-ports PUBLIC btrfsbackup-config-domain btrfsbackup-core)

add_library(btrfsbackup-config STATIC
    application_config.cpp
    profile_fingerprint.cpp
    profile_artifact_renderer.cpp
    profile_render.cpp
    wizard/profile_wizard_paths.cpp
    wizard/profile_wizard_prompt.cpp
)

target_compile_features(btrfsbackup-config PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config
    PUBLIC_HEADERS
        config/application_config.hpp
        config/application_paths.hpp
        config/profile_artifact_renderer.hpp
        config/profile_fingerprint.hpp
        config/profile_render.hpp
        config/wizard/profile_wizard_paths.hpp
        config/wizard/profile_wizard_prompt.hpp
)
target_link_libraries(btrfsbackup-config
    PUBLIC
        btrfsbackup-config-domain
        btrfsbackup-config-ports
        btrfsbackup-config-wizard
    PRIVATE
        btrfsbackup-config-json
)

add_library(btrfsbackup-config-linux STATIC
    ../platform/linux/config/application_config.cpp
    ../platform/linux/config/installation_render.cpp
    ../platform/linux/config/installation_service.cpp
    ../platform/linux/config/installation_validate.cpp
    ../platform/linux/config/profile_artifact_io.cpp
    ../platform/linux/config/profile_configuration_transaction.cpp
    ../platform/linux/config/profile_installer.cpp
    ../platform/linux/config/profile_repository.cpp
    ../platform/linux/config/profile_service.cpp
    ../platform/linux/config/profile_wizard_install.cpp
    ../platform/linux/config/profile_wizard.cpp
    ../platform/linux/config/profile_wizard_device.cpp
    ../platform/linux/config/profile_wizard_sources.cpp
    ../platform/linux/config/render_directory.cpp
)

target_compile_features(btrfsbackup-config-linux PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config-linux
    PUBLIC_HEADERS
        platform/linux/config/application_config.hpp
        platform/linux/config/installation_render.hpp
        platform/linux/config/installation_service.hpp
        platform/linux/config/installation_validate.hpp
        platform/linux/config/profile_artifact_io.hpp
        platform/linux/config/profile_configuration_transaction.hpp
        platform/linux/config/profile_installer.hpp
        platform/linux/config/profile_repository.hpp
        platform/linux/config/profile_service.hpp
        platform/linux/config/profile_wizard.hpp
        platform/linux/config/profile_wizard_device.hpp
        platform/linux/config/profile_wizard_install.hpp
        platform/linux/config/profile_wizard_sources.hpp
        platform/linux/config/render_directory.hpp
)
target_link_libraries(btrfsbackup-config-linux
    PUBLIC
        btrfsbackup-config
        btrfsbackup-config-json
        btrfsbackup-platform-linux
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-config-domain PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config-json PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config-wizard PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config-ports PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config-linux PRIVATE -Wall -Wextra -Wpedantic)
endif()
