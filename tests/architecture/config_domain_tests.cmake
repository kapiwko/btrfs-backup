# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

file(READ "${PROJECT_SOURCE_DIR}/src/config/model/profile.hpp" profile_header)
if(profile_header MATCHES "config/model/json.hpp|nlohmann")
    message(FATAL_ERROR "config domain profile must not expose a JSON dependency")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/config/CMakeLists.txt" config_cmake)
if(NOT config_cmake MATCHES "target_link_libraries\\(btrfsbackup-config-domain[^\\)]*\\)")
    message(FATAL_ERROR "cannot find config domain link declaration")
endif()

set(domain_links "${CMAKE_MATCH_0}")
if(domain_links MATCHES "config-json|nlohmann|platform")
    message(FATAL_ERROR "config domain target links a forbidden JSON or platform dependency")
endif()
