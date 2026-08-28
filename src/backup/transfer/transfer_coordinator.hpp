// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/model/backup_run_actions.hpp>
#include <backup/ports/safe_directory.hpp>
#include <backup/transfer/async_transfer.hpp>
#include <core/cancellation.hpp>

namespace btrfsbackup::backup::transfer {

class TransferCoordinator {
  public:
    TransferCoordinator(
        IAsyncTransferPipeline& pipeline,
        const ISafeDirectoryRootFactory& safe_directories
    );

    [[nodiscard]] TransferResult execute(
        const SendReceiveAction& action,
        const std::filesystem::path& target_mount_point,
        ITransferEventSink& events,
        CancellationToken& cancellation
    );

  private:
    IAsyncTransferPipeline& pipeline_;
    const ISafeDirectoryRootFactory& safe_directories_;
};

} // namespace btrfsbackup::backup::transfer
