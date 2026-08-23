#include <btrfsbackup/pending_recovery_plan.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/run_state.hpp>
#include <btrfsbackup/validation.hpp>

namespace fs = std::filesystem;

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool remote_contains_received_uuid(
    const std::vector<btrfsbackup::SnapshotInfo>& remote_snapshots,
    const std::string& source_id,
    const std::string& uuid
) {
    const std::string wanted = lowercase(uuid);
    for (const btrfsbackup::SnapshotInfo& remote : remote_snapshots) {
        if (remote.source_id == source_id
            && !remote.received_uuid.empty()
            && lowercase(remote.received_uuid) == wanted) {
            return true;
        }
    }
    return false;
}

bool marker_path_is_valid(
    const btrfsbackup::PendingMarker& marker,
    const std::string& source_id,
    const fs::path& local_snapshot_dir
) {
    if (marker.source_name != source_id || marker.local_snapshot_path.empty()) {
        return false;
    }

    const fs::path snapshot_path = fs::path(marker.local_snapshot_path).lexically_normal();
    if (!btrfsbackup::path_is_within(snapshot_path, local_snapshot_dir)) {
        return false;
    }

    const std::string base = snapshot_path.filename().string();
    return base.rfind(source_id + "-", 0) == 0;
}

} // namespace

namespace btrfsbackup {

std::optional<PendingMarker> read_pending_marker_if_exists(
    const fs::path& profile_state_dir,
    const std::string& source_id
) {
    validate_identifier(source_id, "sourceId");
    const fs::path marker_path = pending_marker_path(profile_state_dir, source_id);

    std::error_code ec;
    if (!fs::is_regular_file(marker_path, ec) || ec) {
        return std::nullopt;
    }

    PendingMarker marker{
        .source_name = read_pending_marker_field(marker_path, "source_name"),
        .local_snapshot_path = read_pending_marker_field(marker_path, "local_snapshot_path"),
        .run_id = read_pending_marker_field(marker_path, "run_id"),
        .timestamp = read_pending_marker_field(marker_path, "timestamp"),
    };
    return marker;
}

PendingRecoveryPlan plan_pending_recovery(
    const std::string& source_id,
    const fs::path& profile_state_dir,
    const fs::path& local_snapshot_dir,
    const std::optional<PendingMarker>& marker,
    const std::optional<SnapshotMetadata>& pending_snapshot,
    const std::vector<SnapshotInfo>& remote_snapshots,
    bool keep_failed_local_snapshot
) {
    validate_identifier(source_id, "sourceId");

    PendingRecoveryPlan plan{
        .action = PendingRecoveryAction::NoMarker,
        .clear_marker = false,
        .delete_local_snapshot = false,
        .marker_path = pending_marker_path(profile_state_dir, source_id),
        .local_snapshot_path = {},
        .message = {},
    };

    if (!marker.has_value()) {
        return plan;
    }

    plan.clear_marker = true;
    plan.local_snapshot_path = marker->local_snapshot_path;

    if (!marker_path_is_valid(*marker, source_id, local_snapshot_dir)) {
        plan.action = PendingRecoveryAction::ClearInvalidMarker;
        plan.message = "Ignoring invalid pending marker for " + source_id + ": " + plan.marker_path.string();
        return plan;
    }

    if (!pending_snapshot.has_value() || !pending_snapshot->is_subvolume) {
        plan.action = PendingRecoveryAction::ClearMissingSnapshot;
        plan.message = "Clearing pending marker for missing local snapshot: " + marker->local_snapshot_path;
        return plan;
    }

    if (!pending_snapshot->uuid.empty()
        && remote_contains_received_uuid(remote_snapshots, source_id, pending_snapshot->uuid)) {
        plan.action = PendingRecoveryAction::PreserveCommittedSnapshot;
        plan.message = "Recovered committed snapshot from an interrupted run: " + marker->local_snapshot_path;
        return plan;
    }

    if (keep_failed_local_snapshot) {
        plan.action = PendingRecoveryAction::KeepFailedLocalSnapshot;
        plan.message = "Keeping pending local snapshot by configuration: " + marker->local_snapshot_path;
        return plan;
    }

    plan.action = PendingRecoveryAction::DeleteOrphanSnapshot;
    plan.delete_local_snapshot = true;
    plan.message = "Removing orphaned local snapshot from an interrupted run: " + marker->local_snapshot_path;
    return plan;
}

} // namespace btrfsbackup
