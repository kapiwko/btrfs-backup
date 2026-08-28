// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/repository_action_handler.hpp>

#include <optional>
#include <string>
#include <utility>

#include <backup/ports/btrfs_operations.hpp>
#include <backup/ports/filesystem.hpp>
#include <backup/ports/pending_marker_store.hpp>
#include <backup/ports/safe_directory.hpp>
#include <backup/snapshot_transfer.hpp>
#include <core/errors.hpp>

namespace btrfsbackup::backup {

namespace {

void cleanup_directory_contents(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    const std::filesystem::path& directory,
    const ISafeDirectoryRoot* safe_root
);

void cleanup_path(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    const std::filesystem::path& path,
    const ISafeDirectoryRoot* safe_root
) {
    if (safe_root != nullptr) {
        safe_root->remove_tree(path);
        return;
    }
    if (!filesystem.exists(path)) {
        return;
    }
    if (btrfs.is_subvolume(path)) {
        btrfs.delete_subvolume(path);
        return;
    }
    if (filesystem.is_directory(path)) {
        cleanup_directory_contents(btrfs, filesystem, path, nullptr);
        filesystem.remove_directory(path);
        return;
    }
    filesystem.remove_tree(path);
}

void cleanup_directory_contents(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    const std::filesystem::path& directory,
    const ISafeDirectoryRoot* safe_root
) {
    if (safe_root != nullptr) {
        safe_root->remove_contents(directory);
        return;
    }
    if (!filesystem.is_directory(directory)) {
        return;
    }
    for (const std::filesystem::path& entry : filesystem.list_directory(directory)) {
        cleanup_path(btrfs, filesystem, entry, nullptr);
    }
}

void cleanup_incoming_run_directory(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    const std::filesystem::path& directory,
    const ISafeDirectoryRoot* safe_root
) {
    if (safe_root != nullptr) {
        safe_root->remove_tree(directory);
        return;
    }
    if (!filesystem.is_directory(directory)) {
        return;
    }
    cleanup_directory_contents(btrfs, filesystem, directory, nullptr);
    filesystem.remove_directory(directory);
}

SnapshotMetadata require_snapshot_metadata(
    IBtrfsOperations& btrfs,
    const std::filesystem::path& path,
    const std::string& message,
    const ISafeDirectoryRoot* safe_root
) {
    std::optional<SnapshotMetadata> metadata = safe_root == nullptr
        ? btrfs.read_snapshot_metadata(path)
        : btrfs.read_snapshot_metadata_beneath(*safe_root, path);
    if (!metadata.has_value()) {
        throw ValidationError(message + ": " + path.string());
    }
    return *metadata;
}

} // namespace

RepositoryActionHandler::RepositoryActionHandler(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    IPendingMarkerStore& pending_markers
)
    : btrfs_(btrfs), filesystem_(filesystem), pending_markers_(pending_markers) {
}

RepositoryActionHandler::RepositoryActionHandler(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    IPendingMarkerStore& pending_markers,
    std::unique_ptr<ISafeDirectoryRoot> local_root,
    std::unique_ptr<ISafeDirectoryRoot> target_root
)
    : btrfs_(btrfs),
      filesystem_(filesystem),
      pending_markers_(pending_markers),
      local_root_(std::move(local_root)),
      target_root_(std::move(target_root)) {
}

RepositoryActionHandler::~RepositoryActionHandler() = default;

void RepositoryActionHandler::handle(const CleanupIncomingAction& action) {
    cleanup_directory_contents(btrfs_, filesystem_, action.incoming_directory, target_root_.get());
}

void RepositoryActionHandler::handle(const VerifyReceivedAction& action) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs_,
        action.local_snapshot,
        "Local snapshot metadata is missing",
        local_root_.get()
    );
    SnapshotMetadata received = require_snapshot_metadata(
        btrfs_,
        action.received_snapshot,
        "Received snapshot metadata is missing",
        target_root_.get()
    );
    verify_received_snapshot(std::string(action.source_id.value()), local, received);
}

void RepositoryActionHandler::handle(const CommitReceivedAction& action) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs_,
        action.local_snapshot,
        "Local snapshot metadata is missing",
        local_root_.get()
    );
    if (target_root_ == nullptr) {
        commit_received_snapshot(
            btrfs_,
            filesystem_,
            action.received_snapshot,
            action.final_snapshot,
            local.uuid
        );
    } else {
        commit_received_snapshot_beneath(
            btrfs_,
            *target_root_,
            action.received_snapshot,
            action.final_snapshot,
            local.uuid
        );
    }
}

void RepositoryActionHandler::handle(const CleanupSourceAction& action) {
    cleanup_path(btrfs_, filesystem_, action.received_snapshot, target_root_.get());
    cleanup_incoming_run_directory(
        btrfs_,
        filesystem_,
        action.incoming_run_directory,
        target_root_.get()
    );
    pending_markers_.clear(action.pending_marker);
}

} // namespace btrfsbackup::backup
