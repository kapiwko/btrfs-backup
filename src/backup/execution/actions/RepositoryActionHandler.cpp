// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/execution/actions/RepositoryActionHandler.hpp>

#include <optional>
#include <string>

#include <backup/ports/IBtrfsOperations.hpp>
#include <backup/ports/IPendingMarkerStore.hpp>
#include <backup/ports/SafeDirectory.hpp>
#include <backup/SnapshotTransfer.hpp>
#include <core/Errors.hpp>

namespace btrfsbackup::backup::execution {

namespace {

SnapshotMetadata require_snapshot_metadata(
    IBtrfsOperations& btrfs,
    const ISafeDirectoryRoot& safe_root,
    const std::filesystem::path& path,
    const std::string& message
) {
    std::optional<SnapshotMetadata> metadata = btrfs.read_snapshot_metadata_beneath(safe_root, path);
    if (!metadata.has_value()) {
        throw ValidationError(message + ": " + path.string());
    }
    return *metadata;
}

} // namespace

RepositoryActionHandler::RepositoryActionHandler(
    IBtrfsOperations& btrfs,
    IPendingMarkerStore& pending_markers,
    ISafeDirectoryRoot& local_root,
    ISafeDirectoryRoot& target_root
)
    : btrfs_(btrfs),
      pending_markers_(pending_markers),
      local_root_(local_root),
      target_root_(target_root) {
}

void RepositoryActionHandler::handle(const CleanupIncomingAction& action) {
    target_root_.remove_contents(action.incoming_directory);
}

void RepositoryActionHandler::handle(const VerifyReceivedAction& action) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs_,
        local_root_,
        action.local_snapshot,
        "Local snapshot metadata is missing"
    );
    SnapshotMetadata received = require_snapshot_metadata(
        btrfs_,
        target_root_,
        action.received_snapshot,
        "Received snapshot metadata is missing"
    );
    verify_received_snapshot(action.source_id, local, received);
}

void RepositoryActionHandler::handle(const CommitReceivedAction& action) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs_,
        local_root_,
        action.local_snapshot,
        "Local snapshot metadata is missing"
    );
    commit_received_snapshot_beneath(
        btrfs_,
        target_root_,
        action.received_snapshot,
        action.final_snapshot,
        local.uuid
    );
}

void RepositoryActionHandler::handle(const CleanupSourceAction& action) {
    target_root_.remove_tree(action.received_snapshot);
    target_root_.remove_tree(action.incoming_run_directory);
    pending_markers_.clear(action.pending_marker);
}

} // namespace btrfsbackup::backup::execution
