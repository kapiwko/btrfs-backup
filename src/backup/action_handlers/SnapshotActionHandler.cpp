// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/SnapshotActionHandler.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include <backup/ports/IBtrfsOperations.hpp>
#include <backup/ports/IFileSystem.hpp>
#include <backup/ports/IPendingMarkerStore.hpp>
#include <backup/ports/SafeDirectory.hpp>
#include <core/Errors.hpp>
#include <core/RuntimeTime.hpp>

namespace btrfsbackup::backup {

namespace {

SnapshotMetadata require_snapshot_metadata(
    IBtrfsOperations& btrfs,
    const std::filesystem::path& path,
    const ISafeDirectoryRoot* safe_root
) {
    std::optional<SnapshotMetadata> metadata = safe_root == nullptr
        ? btrfs.read_snapshot_metadata(path)
        : btrfs.read_snapshot_metadata_beneath(*safe_root, path);
    if (!metadata.has_value()) {
        throw ValidationError("New local snapshot metadata is missing: " + path.string());
    }
    return *metadata;
}

} // namespace

SnapshotActionHandler::SnapshotActionHandler(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    IPendingMarkerStore& pending_markers
)
    : btrfs_(btrfs), filesystem_(filesystem), pending_markers_(pending_markers) {
}

SnapshotActionHandler::SnapshotActionHandler(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    IPendingMarkerStore& pending_markers,
    std::unique_ptr<ISafeDirectoryRoot> local_root
)
    : btrfs_(btrfs),
      filesystem_(filesystem),
      pending_markers_(pending_markers),
      local_root_(std::move(local_root)) {
}

SnapshotActionHandler::~SnapshotActionHandler() = default;

void SnapshotActionHandler::handle(const CreateSnapshotAction& action) {
    if (local_root_ == nullptr) {
        filesystem_.create_directories(action.snapshot_directory);
    } else {
        local_root_->ensure_directory(action.snapshot_directory);
    }
    pending_markers_.write(
        action.profile_state_directory,
        PendingMarker{
            .source_id = action.source_id,
            .local_snapshot_path = action.snapshot,
            .final_snapshot_path = action.final_remote_snapshot,
            .run_id = action.run_id,
            .timestamp = std::chrono::system_clock::now(),
        }
    );

    if (local_root_ == nullptr) {
        btrfs_.create_readonly_snapshot(action.source, action.snapshot);
    } else {
        btrfs_.create_readonly_snapshot_beneath(
            *local_root_,
            action.source,
            *local_root_,
            action.snapshot
        );
    }
    SnapshotMetadata metadata = require_snapshot_metadata(btrfs_, action.snapshot, local_root_.get());
    if (!metadata.is_subvolume || !metadata.readonly) {
        throw ValidationError("New local snapshot is not readonly: " + action.snapshot.string());
    }
}

} // namespace btrfsbackup::backup
