// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/FilePendingMarkerStore.hpp>

#include <filesystem>

#include <core/Identifiers.hpp>
#include <state/RunState.hpp>

namespace btrfsbackup::state {

FilePendingMarkerStore::FilePendingMarkerStore(IPersistentDocumentOperations& files) : files_(files) {
}

std::optional<btrfsbackup::backup::PendingMarker> FilePendingMarkerStore::read(
    const std::filesystem::path& profile_state_dir,
    const SourceId& source_id
) const {
    const std::filesystem::path path = pending_marker_path(profile_state_dir, source_id);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return std::nullopt;
    }
    const std::optional<RuntimeTimePoint> timestamp = parse_utc_timestamp(read_pending_marker_field(path, "timestamp"));
    if (!timestamp.has_value()) {
        return std::nullopt;
    }
    return btrfsbackup::backup::PendingMarker{
        .source_id = SourceId{read_pending_marker_field(path, "source_name")},
        .local_snapshot_path = read_pending_marker_field(path, "local_snapshot_path"),
        .final_snapshot_path = read_pending_marker_field(path, "final_snapshot_path"),
        .run_id = RunId{read_pending_marker_field(path, "run_id")},
        .timestamp = *timestamp,
    };
}

void FilePendingMarkerStore::write(
    const std::filesystem::path& profile_state_dir,
    const btrfsbackup::backup::PendingMarker& marker
) {
    write_pending_marker(files_, profile_state_dir, marker);
}

void FilePendingMarkerStore::clear(const std::filesystem::path& marker_path) {
    clear_pending_marker(files_, marker_path);
}

} // namespace btrfsbackup::state
