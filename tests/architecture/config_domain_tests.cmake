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
