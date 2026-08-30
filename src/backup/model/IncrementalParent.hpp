// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <backup/model/Snapshot.hpp>
#include <core/Identifiers.hpp>

namespace btrfsbackup::backup {

struct FullTransfer {
};

struct IncrementalTransfer {
    SnapshotInfo local_parent;
    SnapshotInfo remote_parent;
};

using IncrementalParentSelection = std::variant<FullTransfer, IncrementalTransfer>;

IncrementalParentSelection select_incremental_parent(
    const SourceId& source_id,
    const std::vector<SnapshotInfo>& local_snapshots,
    const std::vector<SnapshotInfo>& remote_snapshots,
    const std::optional<std::filesystem::path>& current_snapshot_path,
    bool incremental_required
);

} // namespace btrfsbackup::backup
