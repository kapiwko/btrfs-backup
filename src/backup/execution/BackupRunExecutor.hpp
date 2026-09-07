// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <exception>
#include <optional>
#include <string>

#include <backup/execution/BackupActionExecutor.hpp>
#include <backup/execution/BackupRunCheckpointPolicy.hpp>
#include <backup/model/BackupRunExecution.hpp>
#include <backup/model/BackupRunPlan.hpp>
#include <backup/ports/IBackupRunEventSink.hpp>

namespace btrfsbackup::backup::execution {

class BackupRunExecutor {
  public:
    BackupRunExecutor(
        IBackupActionExecutor& action_executor,
        IBackupRunCheckpointStore& checkpoints
    );

    [[nodiscard]] BackupRunExecutionResult execute(
        const BackupRunPlan& plan,
        IBackupRunEventSink& events,
        CancellationToken& cancellation
    );

  private:
    [[nodiscard]] static ErrorCode run_error_code(const std::exception& error);
    static void emit_action_failure(
        IBackupRunEventSink& events,
        const BackupRunPlan& plan,
        const BackupSourceRunPlan& source,
        int source_index,
        BackupRunActionKind action_kind,
        const std::exception& error
    );
    [[nodiscard]] static int source_index_for_event(
        const BackupRunPlan& plan,
        const BackupSourceRunPlan& source
    );
    static void emit_cancelled(
        IBackupRunEventSink& events,
        const BackupRunPlan& plan,
        const BackupSourceRunPlan* source,
        std::optional<BackupRunActionKind> action_kind,
        std::optional<ErrorCode> error_code = std::nullopt,
        const std::string& message = ""
    );

    IBackupActionExecutor& action_executor_;
    BackupRunCheckpointPolicy checkpoint_policy_;
};

} // namespace btrfsbackup::backup::execution
