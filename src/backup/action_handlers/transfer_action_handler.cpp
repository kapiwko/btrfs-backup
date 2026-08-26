// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/transfer_action_handler.hpp>

#include <backup/ports/filesystem.hpp>
#include <platform/linux/safe_directory_root.hpp>

namespace btrfsbackup {

TransferActionHandler::TransferActionHandler(IFileSystem& filesystem) : filesystem_(filesystem) {
}

TransferActionHandler::TransferActionHandler(
    IFileSystem& filesystem,
    const std::filesystem::path& target_root
)
    : filesystem_(filesystem),
      target_root_(std::make_unique<SafeDirectoryRoot>(target_root)) {
}

TransferActionHandler::~TransferActionHandler() = default;

void TransferActionHandler::handle(const SendReceiveAction& action) {
    if (target_root_ == nullptr) {
        filesystem_.create_directories(action.remote_snapshot_directory);
        filesystem_.create_directories(action.incoming_run_directory);
    } else {
        target_root_->ensure_directory(action.remote_snapshot_directory);
        target_root_->ensure_directory(action.incoming_run_directory);
    }
}

} // namespace btrfsbackup
