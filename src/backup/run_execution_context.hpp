// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <backup/ports/cancellation_request_store.hpp>
#include <backup/ports/cancellation_monitor.hpp>
#include <backup/ports/checkpoint_store_factory.hpp>
#include <backup/ports/run_event_sink_factory.hpp>
#include <backup/ports/run_lease.hpp>
#include <backup/ports/target_manager.hpp>
#include <core/cancellation.hpp>
#include <core/identifiers.hpp>

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
        std::unique_ptr<IMountedTargetSession> target_session,
        ICheckpointStoreFactory& checkpoints,
        ICancellationRequestStore& cancellation_requests,
        ICancellationMonitor& cancellation_monitor
    );

    RunExecutionContext(const RunExecutionContext&) = delete;
    RunExecutionContext& operator=(const RunExecutionContext&) = delete;
    ~RunExecutionContext() noexcept;

    [[nodiscard]] CloseResult close();

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
    bool closed_ = false;
};

} // namespace btrfsbackup::backup
