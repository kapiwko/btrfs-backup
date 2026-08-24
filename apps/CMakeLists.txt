add_executable(btrfs-backupctl btrfs-backupctl.cpp)
set_target_properties(btrfs-backupctl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_link_libraries(btrfs-backupctl PRIVATE btrfsbackup-cli)

add_executable(btrfs-backup-native btrfs-backup.cpp)
set_target_properties(
    btrfs-backup-native
    PROPERTIES
        OUTPUT_NAME btrfs-backup
        RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)
target_link_libraries(btrfs-backup-native PRIVATE btrfsbackup-cli)

install(
    TARGETS btrfs-backupctl btrfs-backup-native
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
