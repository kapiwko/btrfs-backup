// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>

#include <backup/ports/IBtrfsOperations.hpp>

namespace btrfsbackup::platform::linux::storage {

std::optional<btrfsbackup::backup::SnapshotMetadata> read_btrfs_snapshot_metadata(const std::filesystem::path& path);

class LibBtrfsOperations final : public btrfsbackup::backup::IBtrfsOperations {
  public:
    bool is_subvolume(const std::filesystem::path& path) override;
    std::optional<btrfsbackup::backup::SnapshotMetadata> read_snapshot_metadata(const std::filesystem::path& path) override;
    void create_readonly_snapshot(const std::filesystem::path& source, const std::filesystem::path& target) override;
    void delete_subvolume(const std::filesystem::path& path) override;
    bool is_subvolume_beneath(const btrfsbackup::backup::ISafeDirectoryRoot& root, const std::filesystem::path& path) override;
    std::optional<btrfsbackup::backup::SnapshotMetadata> read_snapshot_metadata_beneath(
        const btrfsbackup::backup::ISafeDirectoryRoot& root,
        const std::filesystem::path& path
    ) override;
    void create_readonly_snapshot_beneath(
        const btrfsbackup::backup::ISafeDirectoryRoot& source_root,
        const std::filesystem::path& source,
        const btrfsbackup::backup::ISafeDirectoryRoot& target_root,
        const std::filesystem::path& target
    ) override;
    void delete_subvolume_beneath(const btrfsbackup::backup::ISafeDirectoryRoot& root, const std::filesystem::path& path) override;
};

} // namespace btrfsbackup::platform::linux::storage
