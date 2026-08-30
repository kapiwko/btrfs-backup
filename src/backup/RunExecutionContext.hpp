// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <backup/ports/CancellationRequestStore.hpp>
#include <backup/ports/CancellationMonitor.hpp>
#include <backup/ports/ICheckpointStoreFactory.hpp>
#include <backup/ports/IRunEventSinkFactory.hpp>
#include <backup/ports/RunLease.hpp>
#include <backup/ports/TargetManager.hpp>
#include <core/Cancellation.hpp>
#include <core/Identifiers.hpp>

namespace btrfsbackup::backup {

enum class RunExecutionContextCloseStage {
    CancellationWatch,
    EventSink,
    CheckpointStore,
    ActiveRun,
    CancellationRequest,
    TargetSession,
    Lease,
};

struct RunExecutionContextCloseFailure {
    RunExecutionContextCloseStage stage;
    std::string message;
};

struct CloseResult {
    std::vector<RunExecutionContextCloseFailure> failures;

    [[nodiscard]] bool succeeded() const noexcept {
        return failures.empty();
    }
};

struct RunExecutionContext {
    RunExecutionContext(
        ProfileId profile_id,
        RunId run_id,
        std::unique_ptr<IBackupRunEventSink>& events,
        std::unique_ptr<IBackupRunLease> lease,
        ICheckpointStoreFactory& checkpoints,
        ICancellationRequestStore& cancellation_requests,
        ICancellationMonitor& cancellation_monitor
    );

    RunExecutionContext(const RunExecutionContext&) = delete;
    RunExecutionContext& operator=(const RunExecutionContext&) = delete;
    ~RunExecutionContext() noexcept;

    [[nodiscard]] CloseResult close();
    [[nodiscard]] std::optional<TargetCleanupError> close_target_session() noexcept;
    void attach_target_session(std::unique_ptr<IMountedTargetSession> session);

    ProfileId profile_id;
    RunId run_id;
    CancellationToken cancellation;
    std::unique_ptr<IActiveRunRegistration> active_run;
    std::unique_ptr<ICancellationWatch> cancellation_watch;
    std::unique_ptr<IBackupRunCheckpointStore> checkpoints;
    std::unique_ptr<IBackupRunLease> lease;
    std::unique_ptr<IMountedTargetSession> target_session;

  private:
    std::unique_ptr<IBackupRunEventSink>& events_;
    ICancellationRequestStore& cancellation_requests_;
    bool target_close_attempted_ = false;
    std::optional<TargetCleanupError> target_close_error_;
    bool closed_ = false;
};

} // namespace btrfsbackup::backup
