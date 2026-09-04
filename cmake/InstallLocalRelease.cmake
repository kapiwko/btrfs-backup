# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(variable IN ITEMS SOURCE_DIR DIST_DIR)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

function(run_stage description)
    message(STATUS "[local install] ${description}")
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result COMMAND_ECHO STDOUT)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed with exit status ${result}")
    endif()
endfunction()

foreach(argument IN LISTS LOCAL_INSTALL_BUILD_OPTIONS)
    if(argument STREQUAL "--target" OR argument STREQUAL "--dist-dir")
        message(FATAL_ERROR "${argument} is fixed by the install-local target and cannot be overridden.")
    elseif(argument STREQUAL "--full-tests")
        message(FATAL_ERROR "Run full tests separately before using the desktop-user install-local target.")
    endif()
endforeach()

if(NOT LOCAL_INSTALL_SKIP_BUILD)
    foreach(command_name IN ITEMS pacman sudo systemctl kbuildsycoca6)
        string(TOUPPER "${command_name}" command_variable)
        find_program("${command_variable}_EXECUTABLE" "${command_name}")
        if(NOT ${command_variable}_EXECUTABLE)
            message(FATAL_ERROR "Missing required command: ${command_name}")
        endif()
    endforeach()
    execute_process(COMMAND id -u OUTPUT_VARIABLE effective_uid OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(effective_uid STREQUAL "0")
        message(FATAL_ERROR
            "Run install-local as your desktop user; it invokes sudo only for system operations."
        )
    endif()
    run_stage(
        "Building Arch Linux release packages"
        "${SOURCE_DIR}/tools/release.py"
        --target arch
        ${LOCAL_INSTALL_BUILD_OPTIONS}
    )
endif()

file(GLOB base_packages "${DIST_DIR}/btrfs-backup-[0-9]*.pkg.tar.zst")
file(GLOB kde_packages "${DIST_DIR}/btrfs-backup-kde-[0-9]*.pkg.tar.zst")
list(LENGTH base_packages base_count)
list(LENGTH kde_packages kde_count)
if(NOT base_count EQUAL 1 OR NOT kde_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one base package and one KDE package in ${DIST_DIR}; "
        "found ${base_count} base and ${kde_count} KDE packages."
    )
endif()

if(LOCAL_INSTALL_DRY_RUN)
    if(NOT DEFINED LOCAL_INSTALL_MANIFEST OR LOCAL_INSTALL_MANIFEST STREQUAL "")
        message(FATAL_ERROR "LOCAL_INSTALL_MANIFEST is required in dry-run mode")
    endif()
    file(WRITE "${LOCAL_INSTALL_MANIFEST}"
        "sudo pacman -U --noconfirm -- ${base_packages} ${kde_packages}\n"
        "sudo systemctl daemon-reload\n"
        "sudo systemctl restart btrfs-backupd.service\n"
        "systemctl --user daemon-reload\n"
        "kbuildsycoca6\n"
        "systemctl --user restart btrfs-backup-kde-monitor.service\n"
        "systemctl --user restart plasma-plasmashell.service\n"
    )
    return()
endif()

run_stage("Installing the base and KDE packages"
    sudo pacman -U --noconfirm -- ${base_packages} ${kde_packages})
run_stage("Reloading the system manager" sudo systemctl daemon-reload)
run_stage("Restarting the system manager" sudo systemctl restart btrfs-backupd.service)
run_stage("Reloading user units" systemctl --user daemon-reload)
run_stage("Refreshing the KDE service cache" kbuildsycoca6)
run_stage("Restarting the KDE monitor"
    systemctl --user restart btrfs-backup-kde-monitor.service)
run_stage("Restarting Plasma Shell"
    systemctl --user restart plasma-plasmashell.service)
message(STATUS "[local install] Local release installed successfully")
message(STATUS "Installed packages:\n  ${base_packages}\n  ${kde_packages}")
