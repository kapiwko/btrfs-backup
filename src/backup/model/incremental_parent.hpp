// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <backup/model/snapshot.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

struct IncrementalParentSelection {
    bool incremental = false;
    bool remote_snapshots_exist = false;
    std::optional<SnapshotInfo> local_parent;
    std::optional<SnapshotInfo> remote_parent;
};

IncrementalParentSelection select_incremental_parent(
    const SourceId& source_id,
    const std::vector<SnapshotInfo>& local_snapshots,
    const std::vector<SnapshotInfo>& remote_snapshots,
    const std::optional<std::filesystem::path>& current_snapshot_path,
    bool incremental_required
);

} // namespace btrfsbackup::backup
