// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <backup/model/pending_recovery.hpp>
#include <backup/model/snapshot_inventory.hpp>

namespace btrfsbackup::backup {

using SnapshotInventoryBySource = std::map<std::string, std::vector<SnapshotInfo>>;
using PendingMarkerBySource = std::map<std::string, std::optional<PendingMarker>>;
using PendingSnapshotBySource = std::map<std::string, std::optional<SnapshotMetadata>>;

class BackupPlanningSnapshot {
  public:
    BackupPlanningSnapshot(
        SnapshotInventoryBySource local_inventory,
        SnapshotInventoryBySource remote_inventory,
        PendingMarkerBySource pending_markers,
        PendingSnapshotBySource pending_snapshots,
        std::filesystem::path profile_state_dir
    );

    [[nodiscard]] const SnapshotInventoryBySource& local_inventory() const;
    [[nodiscard]] const SnapshotInventoryBySource& remote_inventory() const;
    [[nodiscard]] const PendingMarkerBySource& pending_markers() const;
    [[nodiscard]] const PendingSnapshotBySource& pending_snapshots() const;
    [[nodiscard]] const std::filesystem::path& profile_state_dir() const;

  private:
    SnapshotInventoryBySource local_inventory_;
    SnapshotInventoryBySource remote_inventory_;
    PendingMarkerBySource pending_markers_;
    PendingSnapshotBySource pending_snapshots_;
    std::filesystem::path profile_state_dir_;
};

} // namespace btrfsbackup::backup
