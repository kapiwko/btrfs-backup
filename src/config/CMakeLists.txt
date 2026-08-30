# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(btrfsbackup-config-domain STATIC
    model/OperationPath.cpp
    model/RepositoryPath.cpp
    model/StoragePolicy.cpp
    model/TargetIdentity.cpp
    model/Validation.cpp
)

target_compile_features(btrfsbackup-config-domain PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config-domain
    PUBLIC_HEADERS
        config/model/ConfigurationGeneration.hpp
        config/model/OperationPath.hpp
        config/model/Profile.hpp
        config/model/RepositoryPath.hpp
        config/model/StoragePolicy.hpp
        config/model/TargetIdentity.hpp
        config/model/Validation.hpp
        config/ApplicationPaths.hpp
        config/ConfigurationIdentity.hpp
)
target_link_libraries(btrfsbackup-config-domain PUBLIC btrfsbackup-core)

add_library(btrfsbackup-config-json STATIC
    model/JsonIo.cpp
    model/ProfileDocumentDecode.cpp
    model/ProfileDocumentEncode.cpp
    model/ProfileDocumentNormalization.cpp
)

target_compile_features(btrfsbackup-config-json PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config-json
    PUBLIC_HEADERS
        config/model/Json.hpp
        config/model/JsonIo.hpp
        config/model/ProfileDocument.hpp
)
target_link_libraries(btrfsbackup-config-json
    PUBLIC
        btrfsbackup-config-domain
        nlohmann_json::nlohmann_json
    PRIVATE
        btrfsbackup-core
)

add_library(btrfsbackup-config-wizard STATIC
    wizard/ProfileWizardModel.cpp
)

target_compile_features(btrfsbackup-config-wizard PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config-wizard
    PUBLIC_HEADERS
        config/wizard/ProfileWizardModel.hpp
)
target_link_libraries(btrfsbackup-config-wizard
    PUBLIC
        btrfsbackup-config-domain
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-core
)

add_library(btrfsbackup-config-ports STATIC
    ports/IProfileRepository.cpp
)

target_compile_features(btrfsbackup-config-ports PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config-ports
    PUBLIC_HEADERS
        config/ports/ConfigurationActivator.hpp
        config/ports/IProfileRepository.hpp
)
target_link_libraries(btrfsbackup-config-ports
    PUBLIC
        btrfsbackup-config-domain
    PRIVATE
        btrfsbackup-core
)

add_library(btrfsbackup-config STATIC
    ApplicationConfig.cpp
    ProfileFingerprint.cpp
    ProfileArtifactRenderer.cpp
    ProfileRender.cpp
    wizard/ProfileWizardPaths.cpp
    wizard/ProfileWizardPrompt.cpp
)

target_compile_features(btrfsbackup-config PUBLIC cxx_std_20)
target_restricted_include_directories(btrfsbackup-config
    PUBLIC_HEADERS
        config/ApplicationConfig.hpp
        config/ProfileArtifactRenderer.hpp
        config/ProfileFingerprint.hpp
        config/ProfileRender.hpp
        config/wizard/ProfileWizardPaths.hpp
        config/wizard/ProfileWizardPrompt.hpp
)
target_link_libraries(btrfsbackup-config
    PUBLIC
        btrfsbackup-config-domain
    PRIVATE
        btrfsbackup-config-json
        btrfsbackup-core
)

add_library(btrfsbackup-platform-linux-config STATIC
    ../platform/linux/config/ApplicationConfig.cpp
    ../platform/linux/config/InstallationRender.cpp
    ../platform/linux/config/InstallationService.cpp
    ../platform/linux/config/InstallationValidate.cpp
    ../platform/linux/config/ProfileArtifactIo.cpp
    ../platform/linux/config/ProfileActivationMigration.cpp
    ../platform/linux/config/ProfileConfigurationTransaction.cpp
    ../platform/linux/config/ProfileInstaller.cpp
    ../platform/linux/config/FileProfileRepository.cpp
    ../platform/linux/config/ProfileRuntimePolicy.cpp
    ../platform/linux/config/ProfileService.cpp
    ../platform/linux/config/ProfileWizardInstall.cpp
    ../platform/linux/config/ProfileWizard.cpp
    ../platform/linux/config/ProfileWizardDevice.cpp
    ../platform/linux/config/ProfileWizardSources.cpp
    ../platform/linux/config/RenderDirectory.cpp
)

target_compile_features(btrfsbackup-platform-linux-config PUBLIC cxx_std_20)
target_compile_definitions(
    btrfsbackup-platform-linux-config
    PRIVATE BTRFSBACKUP_INSTALL_BINDIR="${CMAKE_INSTALL_FULL_BINDIR}"
)
target_restricted_include_directories(btrfsbackup-platform-linux-config
    PUBLIC_HEADERS
        platform/linux/config/ApplicationConfig.hpp
        platform/linux/config/InstallationRender.hpp
        platform/linux/config/InstallationService.hpp
        platform/linux/config/InstallationValidate.hpp
        platform/linux/config/ProfileArtifactIo.hpp
        platform/linux/config/ProfileActivationMigration.hpp
        platform/linux/config/ProfileConfigurationTransaction.hpp
        platform/linux/config/ProfileInstaller.hpp
        platform/linux/config/FileProfileRepository.hpp
        platform/linux/config/ProfileRuntimePolicy.hpp
        platform/linux/config/ProfileService.hpp
        platform/linux/config/ProfileWizard.hpp
        platform/linux/config/ProfileWizardDevice.hpp
        platform/linux/config/ProfileWizardInstall.hpp
        platform/linux/config/ProfileWizardSources.hpp
        platform/linux/config/RenderDirectory.hpp
    PRIVATE_HEADERS
        platform/linux/config/ProfileLegacyRuntimePolicy.hpp
)
target_link_libraries(btrfsbackup-platform-linux-config
    PUBLIC
        btrfsbackup-config
        btrfsbackup-config-ports
    PRIVATE
        btrfsbackup-config-domain
        btrfsbackup-config-json
        btrfsbackup-config-wizard
        btrfsbackup-core
        btrfsbackup-platform-linux
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-config-domain PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config-json PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config-wizard PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config-ports PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-config PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(btrfsbackup-platform-linux-config PRIVATE -Wall -Wextra -Wpedantic)
endif()
