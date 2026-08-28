// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/file_pending_marker_store.hpp>

#include <filesystem>

#include <core/identifiers.hpp>
#include <state/run_state.hpp>

namespace btrfsbackup::state {

FilePendingMarkerStore::FilePendingMarkerStore(IPersistentDocumentOperations& files) : files_(files) {
}

std::optional<btrfsbackup::backup::PendingMarker> FilePendingMarkerStore::read(
    const std::filesystem::path& profile_state_dir,
    const std::string& source_id
) const {
    validate_identifier(source_id, "sourceId");
    const std::filesystem::path path = pending_marker_path(profile_state_dir, source_id);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return std::nullopt;
    }
    return btrfsbackup::backup::PendingMarker{
        .source_name = read_pending_marker_field(path, "source_name"),
        .local_snapshot_path = read_pending_marker_field(path, "local_snapshot_path"),
        .final_snapshot_path = read_pending_marker_field(path, "final_snapshot_path"),
        .run_id = read_pending_marker_field(path, "run_id"),
        .timestamp = read_pending_marker_field(path, "timestamp"),
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
