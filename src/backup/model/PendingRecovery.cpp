// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/model/PendingRecovery.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <core/Identifiers.hpp>
#include <config/model/Validation.hpp>

namespace fs = std::filesystem;

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool remote_contains_received_uuid(
    const std::vector<btrfsbackup::backup::SnapshotInfo>& remote_snapshots,
    const btrfsbackup::SourceId& source_id,
    const std::string& uuid
) {
    const std::string wanted = lowercase(uuid);
    for (const btrfsbackup::backup::SnapshotInfo& remote : remote_snapshots) {
        if (remote.source_id == source_id && !remote.received_uuid.empty() && lowercase(remote.received_uuid) == wanted) {
            return true;
        }
    }
    return false;
}

bool uuid_equals(const std::string& left, const std::string& right) {
    return !left.empty() && !right.empty() && lowercase(left) == lowercase(right);
}

bool marker_path_is_valid(
    const btrfsbackup::backup::PendingMarker& marker,
    const btrfsbackup::SourceId& source_id,
    const fs::path& local_snapshot_dir,
    const fs::path& remote_snapshot_dir
) {
    const std::string source_id_value{source_id.value()};
    if (marker.source_name != source_id_value || marker.local_snapshot_path.empty()) {
        return false;
    }

    const fs::path snapshot_path = fs::path(marker.local_snapshot_path).lexically_normal();
    if (!btrfsbackup::config::path_is_within(snapshot_path, local_snapshot_dir)) {
        return false;
    }

    const std::string base = snapshot_path.filename().string();
    if (base.rfind(source_id_value + "-", 0) != 0) {
        return false;
    }

    // Markers written before final_snapshot_path was introduced retain the
    // previous UUID-based recovery behavior.
    if (marker.final_snapshot_path.empty()) {
        return true;
    }

    const fs::path final_path = fs::path(marker.final_snapshot_path).lexically_normal();
    return btrfsbackup::config::path_is_within(final_path, remote_snapshot_dir) && final_path.filename() == snapshot_path.filename();
}

const btrfsbackup::backup::SnapshotInfo* remote_snapshot_at_path(
    const std::vector<btrfsbackup::backup::SnapshotInfo>& remote_snapshots,
    const fs::path& path
) {
    const fs::path wanted = path.lexically_normal();
    for (const btrfsbackup::backup::SnapshotInfo& remote : remote_snapshots) {
        if (remote.path.lexically_normal() == wanted) {
            return &remote;
        }
    }
    return nullptr;
}

} // namespace

namespace btrfsbackup::backup {

PendingRecoveryPlan plan_pending_recovery(
    const SourceId& source_id,
    const fs::path& profile_state_dir,
    const fs::path& local_snapshot_dir,
    const fs::path& remote_snapshot_dir,
    const std::optional<PendingMarker>& marker,
    const std::optional<SnapshotMetadata>& pending_snapshot,
    const std::vector<SnapshotInfo>& remote_snapshots,
    bool keep_failed_local_snapshot
) {
    const std::string source_id_value{source_id.value()};
    PendingRecoveryPlan plan{
        .marker_path = profile_state_dir / ("pending-" + source_id_value),
        .pending_snapshot_path = {},
        .effects = {},
        .message = {},
    };

    if (!marker.has_value()) {
        return plan;
    }

    plan.pending_snapshot_path = marker->local_snapshot_path;
    plan.effects.emplace_back(ClearPendingMarker{plan.marker_path});

    if (!marker_path_is_valid(*marker, source_id, local_snapshot_dir, remote_snapshot_dir)) {
        plan.message = "Ignoring invalid pending marker for " + source_id_value + ": " + plan.marker_path.string();
        return plan;
    }

    if (!pending_snapshot.has_value() || !pending_snapshot->is_subvolume) {
        plan.message = "Clearing pending marker for missing local snapshot: " + marker->local_snapshot_path;
        return plan;
    }

    const fs::path remote_snapshot_path = marker->final_snapshot_path;
    const SnapshotInfo* final_snapshot = remote_snapshot_at_path(remote_snapshots, remote_snapshot_path);
    if (final_snapshot != nullptr && !pending_snapshot->uuid.empty() && !uuid_equals(final_snapshot->received_uuid, pending_snapshot->uuid)) {
        plan.effects.insert(plan.effects.begin(), DeletePendingRemoteSnapshot{remote_snapshot_path});
        if (!keep_failed_local_snapshot && !remote_contains_received_uuid(remote_snapshots, source_id, pending_snapshot->uuid)) {
            plan.effects.insert(plan.effects.begin() + 1, DeletePendingLocalSnapshot{plan.pending_snapshot_path});
        }
        plan.message = "Removing unverified committed snapshot left by an interrupted run: " + remote_snapshot_path.string();
        return plan;
    }

    if (!pending_snapshot->uuid.empty() && remote_contains_received_uuid(remote_snapshots, source_id, pending_snapshot->uuid)) {
        plan.message = "Recovered committed snapshot from an interrupted run: " + marker->local_snapshot_path;
        return plan;
    }

    if (keep_failed_local_snapshot) {
        plan.message = "Keeping pending local snapshot by configuration: " + marker->local_snapshot_path;
        return plan;
    }

    plan.effects.insert(plan.effects.begin(), DeletePendingLocalSnapshot{plan.pending_snapshot_path});
    plan.message = "Removing orphaned local snapshot from an interrupted run: " + marker->local_snapshot_path;
    return plan;
}

} // namespace btrfsbackup::backup
