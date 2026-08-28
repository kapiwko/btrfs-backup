// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/model/retention_plan.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <core/errors.hpp>
#include <core/identifiers.hpp>

namespace {

bool oldest_first(const btrfsbackup::backup::SnapshotInfo& left, const btrfsbackup::backup::SnapshotInfo& right) {
    if (left.timestamp != right.timestamp) {
        return left.timestamp < right.timestamp;
    }
    if (left.sequence != right.sequence) {
        return left.sequence < right.sequence;
    }
    return left.path.string() < right.path.string();
}

} // namespace

namespace btrfsbackup::backup {

RetentionPlan plan_count_retention(
    const SourceId& source_id,
    const std::vector<SnapshotInfo>& snapshots,
    std::size_t keep_count
) {
    std::vector<SnapshotInfo> matching;
    for (const SnapshotInfo& snapshot : snapshots) {
        if (snapshot.source_id == source_id) {
            matching.push_back(snapshot);
        }
    }
    std::sort(matching.begin(), matching.end(), oldest_first);

    RetentionPlan plan{
        .source_id = source_id,
        .keep_count = keep_count,
        .keep = {},
        .delete_snapshots = {},
    };

    if (keep_count == 0 || matching.size() <= keep_count) {
        plan.keep = matching;
        return plan;
    }

    const auto delete_count = static_cast<std::vector<SnapshotInfo>::difference_type>(matching.size() - keep_count);
    plan.delete_snapshots.assign(matching.begin(), matching.begin() + delete_count);
    plan.keep.assign(matching.begin() + delete_count, matching.end());
    return plan;
}

} // namespace btrfsbackup::backup
