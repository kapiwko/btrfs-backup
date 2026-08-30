// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>

#include <backup/model/SnapshotInventory.hpp>
#include <backup/ports/SafeDirectory.hpp>

namespace btrfsbackup::backup {

class IBtrfsOperations {
  public:
    virtual ~IBtrfsOperations() = default;
    virtual bool is_subvolume(const std::filesystem::path& path) = 0;
    virtual std::optional<SnapshotMetadata> read_snapshot_metadata(const std::filesystem::path& path) = 0;
    virtual void create_readonly_snapshot(const std::filesystem::path& source, const std::filesystem::path& target) = 0;
    virtual void delete_subvolume(const std::filesystem::path& path) = 0;
    virtual bool is_subvolume_beneath(const ISafeDirectoryRoot& root, const std::filesystem::path& path);
    virtual std::optional<SnapshotMetadata> read_snapshot_metadata_beneath(
        const ISafeDirectoryRoot& root,
        const std::filesystem::path& path
    );
    virtual void create_readonly_snapshot_beneath(
        const ISafeDirectoryRoot& source_root,
        const std::filesystem::path& source,
        const ISafeDirectoryRoot& target_root,
        const std::filesystem::path& target
    );
    virtual void delete_subvolume_beneath(const ISafeDirectoryRoot& root, const std::filesystem::path& path);
};

} // namespace btrfsbackup::backup
