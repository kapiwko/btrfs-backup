// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>

#include <backup/ports/btrfs_operations.hpp>
#include <platform/linux/safe_directory_root.hpp>

namespace btrfsbackup {

std::optional<SnapshotMetadata> read_btrfs_snapshot_metadata(const std::filesystem::path& path);

class LibBtrfsOperations final : public IBtrfsOperations {
  public:
    bool is_subvolume(const std::filesystem::path& path) override;
    std::optional<SnapshotMetadata> read_snapshot_metadata(const std::filesystem::path& path) override;
    void create_readonly_snapshot(const std::filesystem::path& source, const std::filesystem::path& target) override;
    void delete_subvolume(const std::filesystem::path& path) override;
    bool is_subvolume_beneath(const SafeDirectoryRoot& root, const std::filesystem::path& path) override;
    std::optional<SnapshotMetadata> read_snapshot_metadata_beneath(
        const SafeDirectoryRoot& root,
        const std::filesystem::path& path
    ) override;
    void create_readonly_snapshot_beneath(
        const SafeDirectoryRoot& source_root,
        const std::filesystem::path& source,
        const SafeDirectoryRoot& target_root,
        const std::filesystem::path& target
    ) override;
    void delete_subvolume_beneath(const SafeDirectoryRoot& root, const std::filesystem::path& path) override;
};

} // namespace btrfsbackup
