#include <backup/incremental_parent.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <string>

#include <config/errors.hpp>
#include <config/identifiers.hpp>

namespace fs = std::filesystem;

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string normalized_path_string(const fs::path& path) {
    return path.lexically_normal().string();
}

bool same_path_text(const fs::path& left, const fs::path& right) {
    return normalized_path_string(left) == normalized_path_string(right);
}

bool newest_first(const btrfsbackup::SnapshotInfo& left, const btrfsbackup::SnapshotInfo& right) {
    if (left.timestamp != right.timestamp) {
        return left.timestamp > right.timestamp;
    }
    if (left.sequence != right.sequence) {
        return left.sequence > right.sequence;
    }
    return left.path.string() > right.path.string();
}

} // namespace

namespace btrfsbackup {

IncrementalParentSelection select_incremental_parent(
    const std::string& source_id,
    const std::vector<SnapshotInfo>& local_snapshots,
    const std::vector<SnapshotInfo>& remote_snapshots,
    const std::optional<fs::path>& current_snapshot_path,
    bool incremental_required
) {
    validate_identifier(source_id, "sourceId");

    IncrementalParentSelection selection;
    std::map<std::string, SnapshotInfo> remote_by_received_uuid;

    for (const SnapshotInfo& remote : remote_snapshots) {
        if (remote.source_id != source_id) {
            continue;
        }
        selection.remote_snapshots_exist = true;
        if (!remote.readonly || remote.received_uuid.empty()) {
            continue;
        }

        const std::string received_uuid = lowercase(remote.received_uuid);
        auto [it, inserted] = remote_by_received_uuid.emplace(received_uuid, remote);
        if (!inserted && it->second.path != remote.path) {
            throw ValidationError("ambiguous remote parent received UUID for " + source_id + ": " + received_uuid);
        }
    }

    std::vector<SnapshotInfo> candidates;
    for (const SnapshotInfo& local : local_snapshots) {
        if (local.source_id != source_id || !local.readonly || local.uuid.empty()) {
            continue;
        }
        if (current_snapshot_path.has_value() && same_path_text(local.path, *current_snapshot_path)) {
            continue;
        }
        candidates.push_back(local);
    }
    std::sort(candidates.begin(), candidates.end(), newest_first);

    for (const SnapshotInfo& local : candidates) {
        const auto found = remote_by_received_uuid.find(lowercase(local.uuid));
        if (found == remote_by_received_uuid.end()) {
            continue;
        }
        selection.incremental = true;
        selection.local_parent = local;
        selection.remote_parent = found->second;
        return selection;
    }

    if (selection.remote_snapshots_exist && incremental_required) {
        throw ValidationError("Remote snapshots exist for " + source_id + ", but no UUID-matching local parent was found.");
    }

    return selection;
}

} // namespace btrfsbackup
