// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <backup/ports/btrfs_operations.hpp>
#include <backup/ports/filesystem.hpp>
#include <backup/model/snapshot_inventory.hpp>

namespace btrfsbackup::backup {

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
    IFileSystem& fs_effects,
    const std::filesystem::path& received_path,
    const std::filesystem::path& final_path,
    const std::string& expected_received_uuid
);

void commit_received_snapshot_beneath(
    IBtrfsOperations& btrfs,
    const ISafeDirectoryRoot& root,
    const std::filesystem::path& received_path,
    const std::filesystem::path& final_path,
    const std::string& expected_received_uuid
);

} // namespace btrfsbackup::backup
