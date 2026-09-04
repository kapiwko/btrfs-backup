# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED BUILD_DIR OR NOT DEFINED INSTALL_ROOT OR NOT DEFINED INSTALL_LIBDIR OR NOT DEFINED INSTALL_BINDIR_FULL)
    message(FATAL_ERROR "BUILD_DIR, INSTALL_ROOT, INSTALL_LIBDIR, and INSTALL_BINDIR_FULL are required")
endif()

file(REMOVE_RECURSE "${INSTALL_ROOT}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${INSTALL_ROOT}/usr"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed:\n${install_output}\n${install_error}")
endif()

set(required_paths
    "usr/bin/btrfs-backup"
    "usr/bin/btrfs-backupctl"
    "usr/${INSTALL_LIBDIR}/systemd/system/btrfs-backup@.service"
    "usr/${INSTALL_LIBDIR}/systemd/system/btrfs-backup-eject@.service"
    "usr/${INSTALL_LIBDIR}/systemd/system/btrfs-backup-validate@.service"
    "usr/${INSTALL_LIBDIR}/systemd/system/btrfs-backup-target@.service"
    "usr/share/btrfs-backup/examples/config/profile.schema.json"
    "usr/share/btrfs-backup/examples/config/profile.example.json"
    "usr/share/btrfs-backup/examples/udev/README.md"
    "usr/share/doc/btrfs-backup/README.md"
    "usr/share/doc/btrfs-backup/cpp-layout.md"
    "usr/etc/btrfs-backup/hooks.d"
)
if(BUILD_SYSTEM_MANAGER)
    list(APPEND required_paths
        "usr/bin/btrfs-backupd"
        "usr/bin/btrfs-backup-device-preparation"
        "usr/${INSTALL_LIBDIR}/systemd/system/btrfs-backupd.service"
        "usr/${INSTALL_LIBDIR}/systemd/system/btrfs-backup-device-preparation@.service"
        "usr/share/dbus-1/system-services/io.github.btrfsbackup.Manager1.service"
        "usr/share/dbus-1/system.d/io.github.btrfsbackup.Manager1.conf"
        "usr/share/dbus-1/interfaces/io.github.btrfsbackup.Manager1.xml"
        "usr/share/polkit-1/actions/io.github.btrfsbackup.policy"
    )
endif()
foreach(relative_path IN LISTS required_paths)
    if(NOT EXISTS "${INSTALL_ROOT}/${relative_path}")
        message(FATAL_ERROR "missing installed path: ${relative_path}")
    endif()
endforeach()

foreach(unit_name IN ITEMS btrfs-backup@.service btrfs-backup-eject@.service btrfs-backup-validate@.service)
    set(unit_path "${INSTALL_ROOT}/usr/${INSTALL_LIBDIR}/systemd/system/${unit_name}")
    file(READ "${unit_path}" unit_content)
    if(unit_content MATCHES "\\{\\{" OR unit_content MATCHES "@BTRFSBACKUP_")
        message(FATAL_ERROR "unresolved placeholder in ${unit_name}")
    endif()
endforeach()

file(READ "${INSTALL_ROOT}/usr/${INSTALL_LIBDIR}/systemd/system/btrfs-backup@.service" backup_unit)
if(NOT backup_unit MATCHES "ExecStart=${INSTALL_BINDIR_FULL}/btrfs-backupctl runner execute")
    message(FATAL_ERROR "installed backup unit does not use configured bindir")
endif()

if(BUILD_SYSTEM_MANAGER)
    file(READ "${INSTALL_ROOT}/usr/${INSTALL_LIBDIR}/systemd/system/btrfs-backupd.service" manager_unit)
    file(READ "${INSTALL_ROOT}/usr/${INSTALL_LIBDIR}/systemd/system/btrfs-backup-device-preparation@.service" preparation_unit)
    file(READ "${INSTALL_ROOT}/usr/share/dbus-1/system-services/io.github.btrfsbackup.Manager1.service" activation)
    if(NOT manager_unit MATCHES "ExecStart=${INSTALL_BINDIR_FULL}/btrfs-backupd")
        message(FATAL_ERROR "manager unit does not use configured bindir")
    endif()
    if(NOT manager_unit MATCHES "StateDirectory=btrfs-backup/device-preparations" OR
       NOT manager_unit MATCHES "ReadWritePaths=-/var/lib/btrfs-backup/device-preparations")
        message(FATAL_ERROR "manager unit cannot persist device preparation transactions")
    endif()
    if(NOT preparation_unit MATCHES "ExecStart=${INSTALL_BINDIR_FULL}/btrfs-backup-device-preparation")
        message(FATAL_ERROR "device preparation unit does not use configured bindir")
    endif()
    if(NOT preparation_unit MATCHES "DevicePolicy=closed" OR
       NOT preparation_unit MATCHES "ProtectSystem=strict")
        message(FATAL_ERROR "device preparation unit lost its device or filesystem sandbox")
    endif()
    if(NOT preparation_unit MATCHES "Wants=modprobe@dm_mod.service modprobe@dm_crypt.service" OR
       NOT preparation_unit MATCHES "After=systemd-udevd.service modprobe@dm_mod.service modprobe@dm_crypt.service")
        message(FATAL_ERROR "device preparation unit does not load device-mapper before applying DeviceAllow")
    endif()
    if(preparation_unit MATCHES "DeviceAllow=block-\\* rw")
        message(FATAL_ERROR "device preparation unit grants access to every block device")
    endif()
    if(NOT activation MATCHES "Exec=${INSTALL_BINDIR_FULL}/btrfs-backupd")
        message(FATAL_ERROR "D-Bus activation does not use configured bindir")
    endif()
endif()
