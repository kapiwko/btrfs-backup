# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

file(READ "${PROJECT_SOURCE_DIR}/src/config/model/profile.hpp" profile_header)
if(profile_header MATCHES "config/model/json.hpp|nlohmann")
    message(FATAL_ERROR "config domain profile must not expose a JSON dependency")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/config/configuration_identity.hpp" identity_header)
if(identity_header MATCHES "config/model/json.hpp|nlohmann")
    message(FATAL_ERROR "configuration identity types must not expose a JSON dependency")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/config/ports/profile_repository.hpp" repository_header)
if(repository_header MATCHES "application_paths[ 	]*\\(|fingerprint[ 	]*\\(")
    message(FATAL_ERROR "profile repository must return one atomic LoadedProfile result")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/config/CMakeLists.txt" config_cmake)
if(NOT config_cmake MATCHES "target_link_libraries\\(btrfsbackup-config-domain[^\\)]*\\)")
    message(FATAL_ERROR "cannot find config domain link declaration")
endif()

set(domain_links "${CMAKE_MATCH_0}")
if(domain_links MATCHES "config-json|nlohmann|platform")
    message(FATAL_ERROR "config domain target links a forbidden JSON or platform dependency")
endif()

if(NOT config_cmake MATCHES "add_library\\(btrfsbackup-platform-linux-config[ \t\r\n]+STATIC")
    message(FATAL_ERROR "Linux configuration adapters must use the platform-linux-config target")
endif()
if(config_cmake MATCHES "btrfsbackup-config-linux")
    message(FATAL_ERROR "legacy config-linux target name must not be restored")
endif()

if(NOT config_cmake MATCHES "target_link_libraries\\(btrfsbackup-config[ \t\r\n]+PUBLIC[ \t\r\n]+btrfsbackup-config-domain[ \t\r\n]+PRIVATE[ \t\r\n]+btrfsbackup-config-json[ \t\r\n]+\\)")
    message(FATAL_ERROR "config target must expose only its domain model dependency")
endif()

if(NOT config_cmake MATCHES "target_link_libraries\\(btrfsbackup-platform-linux-config[ \t\r\n]+PUBLIC[ \t\r\n]+btrfsbackup-config[ \t\r\n]+btrfsbackup-config-ports[ \t\r\n]+PRIVATE[ \t\r\n]+btrfsbackup-config-json[ \t\r\n]+btrfsbackup-config-wizard[ \t\r\n]+btrfsbackup-platform-linux[ \t\r\n]+\\)")
    message(FATAL_ERROR "Linux configuration adapter dependencies are not isolated")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/platform/linux/config/profile_repository.hpp" linux_repository_header)
if(linux_repository_header MATCHES "config/model/profile_document.hpp|config/model/json.hpp|nlohmann")
    message(FATAL_ERROR "Linux profile repository header exposes JSON implementation details")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/platform/linux/config/profile_runtime_policy.hpp" runtime_policy_header)
if(runtime_policy_header MATCHES "config/model/json.hpp|nlohmann")
    message(FATAL_ERROR "public Linux runtime policy exposes JSON implementation details")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/config/model/profile_document.cpp" document_source)
if(document_source MATCHES "systemd_|trusted_hook_directory")
    message(FATAL_ERROR "config JSON target contains Linux runtime policy")
endif()
