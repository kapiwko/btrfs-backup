#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <btrfsbackup/btrfs_operations.hpp>
#include <btrfsbackup/runtime_adapters.hpp>
#include <btrfsbackup/snapshot_inventory.hpp>

namespace btrfsbackup {

struct SendReceiveCommandPlan {
    std::vector<std::string> send_argv;
    std::vector<std::string> receive_argv;
};

SendReceiveCommandPlan build_send_receive_command_plan(
    const std::filesystem::path& snapshot_path,
    const std::filesystem::path& parent_path,
    const std::filesystem::path& receive_directory
);

void verify_received_snapshot(
    const std::string& source_id,
    const SnapshotMetadata& local_snapshot,
    const SnapshotMetadata& received_snapshot
);

void commit_received_snapshot(
    IBtrfsOperations& btrfs,
    IFileSystemEffects& fs_effects,
    const std::filesystem::path& received_path,
    const std::filesystem::path& final_path,
    const std::string& expected_received_uuid
);

void commit_received_snapshot_beneath(
    IBtrfsOperations& btrfs,
    const SafeDirectoryRoot& root,
    const std::filesystem::path& received_path,
    const std::filesystem::path& final_path,
    const std::string& expected_received_uuid
);

} // namespace btrfsbackup
