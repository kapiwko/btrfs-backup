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

namespace btrfsbackup::backup::execution {

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

struct RunExecutionContextCloseResult {
    std::vector<RunExecutionContextCloseFailure> failures;

    [[nodiscard]] bool succeeded() const noexcept {
        return failures.empty();
    }
};

class RunExecutionContext {
  public:
    RunExecutionContext(
        ProfileId profile_id,
        RunId run_id,
        std::unique_ptr<IBackupRunLease> lease,
        ICheckpointStoreFactory& checkpoints,
        ICancellationRequestStore& cancellation_requests,
        ICancellationMonitor& cancellation_monitor
    );

    RunExecutionContext(const RunExecutionContext&) = delete;
    RunExecutionContext& operator=(const RunExecutionContext&) = delete;
    RunExecutionContext(RunExecutionContext&&) = delete;
    RunExecutionContext& operator=(RunExecutionContext&&) = delete;
    ~RunExecutionContext() noexcept;

    [[nodiscard]] const RunExecutionContextCloseResult& close();
    [[nodiscard]] std::optional<TargetCleanupError> close_target_session() noexcept;
    void attach_event_sink(std::unique_ptr<IBackupRunEventSink> events) noexcept;
    void attach_target_session(std::unique_ptr<IMountedTargetSession> session) noexcept;
    [[nodiscard]] IBackupRunEventSink& event_sink() const;
    [[nodiscard]] CancellationToken& cancellation_token() noexcept;
    [[nodiscard]] IBackupRunCheckpointStore& checkpoint_store() noexcept;

  private:
    void close_cancellation_watch(RunExecutionContextCloseResult& result);
    void release_event_sink() noexcept;
    void release_checkpoint_store() noexcept;
    void close_active_run(RunExecutionContextCloseResult& result);
    void clear_cancellation_request(RunExecutionContextCloseResult& result);
    void collect_target_session_failure(RunExecutionContextCloseResult& result);
    void release_lease() noexcept;
    void report_close_failures(const RunExecutionContextCloseResult& result) const noexcept;

    ProfileId profile_id_;
    RunId run_id_;
    CancellationToken cancellation_;
    std::unique_ptr<IActiveRunRegistration> active_run_;
    std::unique_ptr<ICancellationWatch> cancellation_watch_;
    std::unique_ptr<IBackupRunCheckpointStore> checkpoints_;
    std::unique_ptr<IBackupRunLease> lease_;
    std::unique_ptr<IMountedTargetSession> target_session_;
    std::unique_ptr<IBackupRunEventSink> events_;
    ICancellationRequestStore& cancellation_requests_;
    bool target_close_attempted_ = false;
    std::optional<TargetCleanupError> target_close_error_;
    bool closed_ = false;
    RunExecutionContextCloseResult close_result_;
};

} // namespace btrfsbackup::backup::execution
