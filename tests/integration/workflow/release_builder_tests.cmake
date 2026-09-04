# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS CMAKE_COMMAND PYTHON SOURCE_DIR TEST_ROOT BUILD_DIR)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

set(root "${TEST_ROOT}/release-builder")
set(first "${root}/first")
set(second "${root}/second")
file(REMOVE_RECURSE "${root}")

function(build_source destination)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env SOURCE_DATE_EPOCH=1700000000
            "${PYTHON}" "${SOURCE_DIR}/tools/release.py"
            --target source --dist-dir "${destination}" --skip-tests
        RESULT_VARIABLE result
        ERROR_VARIABLE error
        OUTPUT_QUIET
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Python source release failed: ${error}")
    endif()
endfunction()

build_source("${first}")
build_source("${second}")
file(READ "${first}/SHA256SUMS" first_checksums)
file(READ "${second}/SHA256SUMS" second_checksums)
if(NOT first_checksums STREQUAL second_checksums)
    message(FATAL_ERROR "Source release is not reproducible for a fixed epoch")
endif()

foreach(name IN ITEMS
        "btrfs-backup-4.0.0.tar.gz"
        "btrfs-backup-4.0.0-source.zip"
        "SBOM.spdx.json"
        "BUILD-REPORT.txt"
        "BUILD-REPORT.json"
        "SHA256SUMS")
    if(NOT EXISTS "${first}/${name}")
        message(FATAL_ERROR "Missing source release artifact: ${name}")
    endif()
endforeach()

file(READ "${first}/BUILD-REPORT.json" report)
foreach(expected IN ITEMS "\"schema\": 1" "\"target\": \"source\"" "\"sha256\"")
    string(FIND "${report}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "JSON build report is missing ${expected}")
    endif()
endforeach()
file(READ "${first}/SBOM.spdx.json" sbom)
string(FIND "${sbom}" "\"spdxVersion\": \"SPDX-2.3\"" spdx_position)
if(spdx_position EQUAL -1)
    message(FATAL_ERROR "Release SBOM is not SPDX 2.3 JSON")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${first}/btrfs-backup-4.0.0.tar.gz"
    OUTPUT_VARIABLE archive_entries
    RESULT_VARIABLE list_result
)
if(NOT list_result EQUAL 0 OR archive_entries MATCHES "__pycache__|[.]pyc")
    message(FATAL_ERROR "Source archive contains transient Python cache files")
endif()

if(NOT EXISTS "${BUILD_DIR}/CPackConfig.cmake")
    message(FATAL_ERROR "CMake did not generate CPackConfig.cmake")
endif()
file(READ "${BUILD_DIR}/CPackConfig.cmake" cpack_config)
foreach(expected IN ITEMS
        "CPACK_ARCHIVE_COMPONENT_INSTALL \"ON\""
        "CPACK_DEB_COMPONENT_INSTALL \"ON\""
        "CPACK_RPM_COMPONENT_INSTALL \"ON\""
        "CPACK_SET_DESTDIR \"ON\"")
    string(FIND "${cpack_config}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "CPack configuration is missing ${expected}")
    endif()
endforeach()
