// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <backup/model/snapshot_inventory.hpp>

namespace btrfsbackup {

struct RetentionPlan {
    std::string source_id;
    std::size_t keep_count = 0;
    std::vector<SnapshotInfo> keep;
    std::vector<SnapshotInfo> delete_snapshots;
};

RetentionPlan plan_count_retention(
    const std::string& source_id,
    const std::vector<SnapshotInfo>& snapshots,
    std::size_t keep_count
);

} // namespace btrfsbackup
