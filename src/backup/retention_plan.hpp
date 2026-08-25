// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <backup/snapshot_inventory.hpp>

namespace btrfsbackup {

struct RetentionPlan {
    std::string source_id;
    long long keep_count = 0;
    std::vector<SnapshotInfo> keep;
    std::vector<SnapshotInfo> delete_snapshots;
};

RetentionPlan plan_count_retention(
    const std::string& source_id,
    const std::vector<SnapshotInfo>& snapshots,
    long long keep_count
);

} // namespace btrfsbackup
