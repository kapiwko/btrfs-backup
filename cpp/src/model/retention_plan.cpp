#include <btrfsbackup/model/retention_plan.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <btrfsbackup/model/errors.hpp>
#include <btrfsbackup/model/identifiers.hpp>

namespace {

bool oldest_first(const btrfsbackup::SnapshotInfo& left, const btrfsbackup::SnapshotInfo& right) {
    if (left.timestamp != right.timestamp) {
        return left.timestamp < right.timestamp;
    }
    if (left.sequence != right.sequence) {
        return left.sequence < right.sequence;
    }
    return left.path.string() < right.path.string();
}

} // namespace

namespace btrfsbackup {

RetentionPlan plan_count_retention(
    const std::string& source_id,
    const std::vector<SnapshotInfo>& snapshots,
    long long keep_count
) {
    validate_identifier(source_id, "sourceId");
    if (keep_count < 0) {
        throw ValidationError("retention keep count must be non-negative");
    }

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

    if (keep_count == 0 || static_cast<long long>(matching.size()) <= keep_count) {
        plan.keep = matching;
        return plan;
    }

    const auto delete_count = static_cast<std::vector<SnapshotInfo>::difference_type>(
        static_cast<long long>(matching.size()) - keep_count
    );
    plan.delete_snapshots.assign(matching.begin(), matching.begin() + delete_count);
    plan.keep.assign(matching.begin() + delete_count, matching.end());
    return plan;
}

} // namespace btrfsbackup
