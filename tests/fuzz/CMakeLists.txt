# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(add_btrfsbackup_fuzzer target source corpus)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${ARGN})
    target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)

    if(BUILD_TESTING)
        set(build_corpus "${CMAKE_CURRENT_BINARY_DIR}/corpus/${corpus}")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${corpus}/" DESTINATION "${build_corpus}")
        add_test(NAME ${target}-smoke COMMAND ${target} -detect_leaks=0 -runs=256 ${build_corpus})
        set_tests_properties(
            ${target}-smoke
            PROPERTIES
                ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
                LABELS fuzz
                TIMEOUT 30
        )
    endif()
endfunction()

add_btrfsbackup_fuzzer(
    btrfsbackup-profile-json-fuzzer
    ProfileJsonFuzzer.cpp
    profile-json
    btrfsbackup-config-json
)
add_btrfsbackup_fuzzer(
    btrfsbackup-path-input-fuzzer
    PathInputFuzzer.cpp
    path-input
    btrfsbackup-config-domain
    btrfsbackup-restore
)
