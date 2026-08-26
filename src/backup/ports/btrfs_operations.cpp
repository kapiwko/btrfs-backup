// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/ports/btrfs_operations.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

bool IBtrfsOperations::is_subvolume_beneath(const ISafeDirectoryRoot&, const fs::path& path) {
    return is_subvolume(path);
}

std::optional<SnapshotMetadata> IBtrfsOperations::read_snapshot_metadata_beneath(
    const ISafeDirectoryRoot&,
    const fs::path& path
) {
    return read_snapshot_metadata(path);
}

void IBtrfsOperations::create_readonly_snapshot_beneath(
    const ISafeDirectoryRoot&,
    const fs::path& source,
    const ISafeDirectoryRoot&,
    const fs::path& target
) {
    create_readonly_snapshot(source, target);
}

void IBtrfsOperations::delete_subvolume_beneath(const ISafeDirectoryRoot&, const fs::path& path) {
    delete_subvolume(path);
}

} // namespace btrfsbackup
