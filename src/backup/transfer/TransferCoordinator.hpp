// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/model/BackupRunActions.hpp>
#include <backup/ports/SafeDirectory.hpp>
#include <backup/transfer/AsyncTransfer.hpp>
#include <core/Cancellation.hpp>

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
