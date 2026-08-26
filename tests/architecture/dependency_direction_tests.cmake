# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(assert_no_layer_includes layer pattern)
    file(GLOB_RECURSE sources
        "${PROJECT_SOURCE_DIR}/src/${layer}/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/${layer}/*.hpp"
    )
    foreach(source IN LISTS sources)
        file(READ "${source}" content)
        if(content MATCHES "#include[ \t]+[<\"](${pattern})/")
            file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
            message(FATAL_ERROR "Forbidden dependency in ${relative}: ${CMAKE_MATCH_1}")
        endif()
    endforeach()
endfunction()

assert_no_layer_includes("core" "config|backup|state|platform|cli|daemon")
assert_no_layer_includes("config" "backup|state|platform|cli|daemon")
assert_no_layer_includes("backup" "state|platform|cli|daemon")
assert_no_layer_includes("backup/model" "backup/ports|state|platform|cli|daemon")
assert_no_layer_includes("state" "platform|cli|daemon")
