// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/model/BackupPlanningSnapshot.hpp>

#include <utility>

namespace btrfsbackup::backup {

BackupPlanningSnapshot::BackupPlanningSnapshot(
    SnapshotInventoryBySource local_inventory,
    SnapshotInventoryBySource remote_inventory,
    PendingMarkerBySource pending_markers,
    PendingSnapshotBySource pending_snapshots,
    std::filesystem::path profile_state_dir
)
    : local_inventory_(std::move(local_inventory)),
      remote_inventory_(std::move(remote_inventory)),
      pending_markers_(std::move(pending_markers)),
      pending_snapshots_(std::move(pending_snapshots)),
      profile_state_dir_(std::move(profile_state_dir)) {
}

const SnapshotInventoryBySource& BackupPlanningSnapshot::local_inventory() const {
    return local_inventory_;
}

const SnapshotInventoryBySource& BackupPlanningSnapshot::remote_inventory() const {
    return remote_inventory_;
}

const PendingMarkerBySource& BackupPlanningSnapshot::pending_markers() const {
    return pending_markers_;
}

const PendingSnapshotBySource& BackupPlanningSnapshot::pending_snapshots() const {
    return pending_snapshots_;
}

const std::filesystem::path& BackupPlanningSnapshot::profile_state_dir() const {
    return profile_state_dir_;
}

} // namespace btrfsbackup::backup
