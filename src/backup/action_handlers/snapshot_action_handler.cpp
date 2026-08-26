// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/snapshot_action_handler.hpp>

#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include <backup/ports/btrfs_operations.hpp>
#include <backup/ports/filesystem.hpp>
#include <core/errors.hpp>
#include <platform/linux/safe_directory_root.hpp>
#include <state/run_state.hpp>

namespace btrfsbackup {

namespace {

std::string current_utc_iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

SnapshotMetadata require_snapshot_metadata(
    IBtrfsOperations& btrfs,
    const std::filesystem::path& path,
    const SafeDirectoryRoot* safe_root
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

SnapshotActionHandler::SnapshotActionHandler(IBtrfsOperations& btrfs, IFileSystem& filesystem)
    : btrfs_(btrfs), filesystem_(filesystem) {
}

SnapshotActionHandler::SnapshotActionHandler(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    const std::filesystem::path& local_root
)
    : btrfs_(btrfs),
      filesystem_(filesystem),
      local_root_(std::make_unique<SafeDirectoryRoot>(local_root)) {
}

SnapshotActionHandler::~SnapshotActionHandler() = default;

void SnapshotActionHandler::handle(const CreateSnapshotAction& action) {
    if (local_root_ == nullptr) {
        filesystem_.create_directories(action.snapshot_directory);
    } else {
        local_root_->ensure_directory(action.snapshot_directory);
    }
    write_pending_marker(
        action.profile_state_directory,
        PendingMarker{
            .source_name = std::string(action.source_id.value()),
            .local_snapshot_path = action.snapshot.string(),
            .final_snapshot_path = action.final_remote_snapshot.string(),
            .run_id = std::string(action.run_id.value()),
            .timestamp = current_utc_iso_timestamp(),
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

} // namespace btrfsbackup
