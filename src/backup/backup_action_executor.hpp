// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/ports/backup_run_event_sink.hpp>
#include <backup/ports/safe_directory.hpp>
#include <backup/transfer/transfer_coordinator.hpp>

namespace btrfsbackup::backup {

struct BackupActionExecutionContext {
    const BackupRunPlan& plan;
    const BackupSourceRunPlan& source;
    int source_index;
    std::uint64_t completed_run_bytes;
    IBackupRunEventSink& events;
    CancellationToken& cancellation;
};

struct BackupActionExecutionResult {
    bool cancelled = false;
    std::uint64_t bytes_transferred = 0;
};

class IBackupActionExecutor {
  public:
    virtual ~IBackupActionExecutor() = default;

    [[nodiscard]] virtual BackupActionExecutionResult execute(
        const BackupRunAction& action,
        BackupActionExecutionContext& context
    ) = 0;
};

class BackupActionExecutor final : public IBackupActionExecutor {
  public:
    BackupActionExecutor(
        IBackupRunActionHandler& action_handler,
        btrfsbackup::backup::transfer::IAsyncTransferPipeline& transfer_pipeline,
        const ISafeDirectoryRootFactory& safe_directories
    );

    [[nodiscard]] BackupActionExecutionResult execute(
        const BackupRunAction& action,
        BackupActionExecutionContext& context
    ) override;

  private:
    IBackupRunActionHandler& action_handler_;
    btrfsbackup::backup::transfer::TransferCoordinator transfer_coordinator_;
};

} // namespace btrfsbackup::backup
