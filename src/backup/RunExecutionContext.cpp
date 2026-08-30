// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/RunExecutionContext.hpp>

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace btrfsbackup::backup {

RunExecutionContext::RunExecutionContext(
    ProfileId profile_id_value,
    RunId run_id_value,
    std::unique_ptr<IBackupRunLease> lease_value,
    ICheckpointStoreFactory& checkpoint_factory,
    ICancellationRequestStore& cancellation_requests,
    ICancellationMonitor& cancellation_monitor
)
    : profile_id_(std::move(profile_id_value)),
      run_id_(std::move(run_id_value)),
      checkpoints_(checkpoint_factory.checkpoints(profile_id_)),
      lease_(std::move(lease_value)),
      cancellation_requests_(cancellation_requests) {
    active_run_ = cancellation_requests.register_active_run({profile_id_, run_id_});
    cancellation_watch_ = cancellation_monitor.watch({profile_id_, run_id_}, cancellation_);
}

RunExecutionContext::~RunExecutionContext() noexcept {
    try {
        (void)close();
    } catch (const std::exception& error) {
        std::fputs("btrfs-backup: run context cleanup failed before diagnostics could be completed: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
    } catch (...) {
        std::fputs("btrfs-backup: run context cleanup failed with an unknown error\n", stderr);
    }
}

const RunExecutionContextCloseResult& RunExecutionContext::close() {
    if (closed_) {
        return close_result_;
    }
    closed_ = true;

    close_cancellation_watch(close_result_);
    release_event_sink();
    release_checkpoint_store();
    close_active_run(close_result_);
    clear_cancellation_request(close_result_);
    collect_target_session_failure(close_result_);
    release_lease();
    report_close_failures(close_result_);
    return close_result_;
}

void RunExecutionContext::attach_event_sink(std::unique_ptr<IBackupRunEventSink> events) noexcept {
    events_ = std::move(events);
}

void RunExecutionContext::attach_target_session(std::unique_ptr<IMountedTargetSession> session) noexcept {
    target_session_ = std::move(session);
}

std::optional<TargetCleanupError> RunExecutionContext::close_target_session() noexcept {
    if (target_close_attempted_) {
        return target_close_error_;
    }
    target_close_attempted_ = true;
    if (target_session_ != nullptr) {
        target_close_error_ = target_session_->close();
        target_session_.reset();
    }
    return target_close_error_;
}

IBackupRunEventSink& RunExecutionContext::event_sink() const {
    if (events_ == nullptr) {
        throw std::logic_error("run execution context has no event sink");
    }
    return *events_;
}

CancellationToken& RunExecutionContext::cancellation_token() noexcept {
    return cancellation_;
}

IBackupRunCheckpointStore& RunExecutionContext::checkpoint_store() noexcept {
    return *checkpoints_;
}

void RunExecutionContext::close_cancellation_watch(RunExecutionContextCloseResult& result) {
    if (cancellation_watch_ == nullptr) {
        return;
    }
    try {
        if (const auto& diagnostic = cancellation_watch_->close()) {
            result.failures.push_back({RunExecutionContextCloseStage::CancellationWatch, diagnostic->message});
        }
    } catch (const std::exception& error) {
        result.failures.push_back({RunExecutionContextCloseStage::CancellationWatch, error.what()});
    } catch (...) {
        result.failures.push_back({RunExecutionContextCloseStage::CancellationWatch, "unknown cleanup failure"});
    }
    cancellation_watch_.reset();
}

void RunExecutionContext::release_event_sink() noexcept {
    events_.reset();
}

void RunExecutionContext::release_checkpoint_store() noexcept {
    checkpoints_.reset();
}

void RunExecutionContext::close_active_run(RunExecutionContextCloseResult& result) {
    if (active_run_ == nullptr) {
        return;
    }
    try {
        if (const auto& diagnostic = active_run_->close()) {
            result.failures.push_back({RunExecutionContextCloseStage::ActiveRun, diagnostic->message});
        }
    } catch (const std::exception& error) {
        result.failures.push_back({RunExecutionContextCloseStage::ActiveRun, error.what()});
    } catch (...) {
        result.failures.push_back({RunExecutionContextCloseStage::ActiveRun, "unknown cleanup failure"});
    }
    active_run_.reset();
}

void RunExecutionContext::clear_cancellation_request(RunExecutionContextCloseResult& result) {
    try {
        cancellation_requests_.clear_cancel_request({profile_id_, run_id_});
    } catch (const std::exception& error) {
        result.failures.push_back({RunExecutionContextCloseStage::CancellationRequest, error.what()});
    } catch (...) {
        result.failures.push_back({RunExecutionContextCloseStage::CancellationRequest, "unknown cleanup failure"});
    }
}

void RunExecutionContext::collect_target_session_failure(RunExecutionContextCloseResult& result) {
    if (std::optional<TargetCleanupError> error = close_target_session()) {
        result.failures.push_back({RunExecutionContextCloseStage::TargetSession, error->message});
    }
}

void RunExecutionContext::release_lease() noexcept {
    lease_.reset();
}

void RunExecutionContext::report_close_failures(const RunExecutionContextCloseResult& result) const noexcept {
    for (const RunExecutionContextCloseFailure& failure : result.failures) {
        std::fputs("btrfs-backup: cleanup failed for profile ", stderr);
        std::fwrite(profile_id_.value().data(), 1, profile_id_.value().size(), stderr);
        std::fputs(", run ", stderr);
        std::fwrite(run_id_.value().data(), 1, run_id_.value().size(), stderr);
        std::fputs(", stage ", stderr);
        switch (failure.stage) {
        case RunExecutionContextCloseStage::CancellationWatch:
            std::fputs("cancellation-watch", stderr);
            break;
        case RunExecutionContextCloseStage::EventSink:
            std::fputs("event-sink", stderr);
            break;
        case RunExecutionContextCloseStage::CheckpointStore:
            std::fputs("checkpoint-store", stderr);
            break;
        case RunExecutionContextCloseStage::ActiveRun:
            std::fputs("active-run", stderr);
            break;
        case RunExecutionContextCloseStage::CancellationRequest:
            std::fputs("cancellation-request", stderr);
            break;
        case RunExecutionContextCloseStage::TargetSession:
            std::fputs("target-session", stderr);
            break;
        case RunExecutionContextCloseStage::Lease:
            std::fputs("lease", stderr);
            break;
        }
        std::fputs(": ", stderr);
        std::fwrite(failure.message.data(), 1, failure.message.size(), stderr);
        std::fputc('\n', stderr);
    }
}

} // namespace btrfsbackup::backup
