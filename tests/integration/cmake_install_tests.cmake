if(NOT DEFINED BUILD_DIR OR NOT DEFINED INSTALL_ROOT OR NOT DEFINED INSTALL_LIBDIR)
    message(FATAL_ERROR "BUILD_DIR, INSTALL_ROOT, and INSTALL_LIBDIR are required")
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
    "usr/share/btrfs-backup/examples/config/profile.schema.json"
    "usr/share/btrfs-backup/examples/config/profile.example.json"
    "usr/share/doc/btrfs-backup/README.md"
    "usr/share/doc/btrfs-backup/cpp-layout.md"
    "usr/etc/btrfs-backup/hooks.d"
)
foreach(relative_path IN LISTS required_paths)
    if(NOT EXISTS "${INSTALL_ROOT}/${relative_path}")
        message(FATAL_ERROR "missing installed path: ${relative_path}")
    endif()
endforeach()

foreach(unit_name IN ITEMS btrfs-backup@.service btrfs-backup-eject@.service)
    set(unit_path "${INSTALL_ROOT}/usr/${INSTALL_LIBDIR}/systemd/system/${unit_name}")
    file(READ "${unit_path}" unit_content)
    if(unit_content MATCHES "\\{\\{")
        message(FATAL_ERROR "unresolved placeholder in ${unit_name}")
    endif()
endforeach()
