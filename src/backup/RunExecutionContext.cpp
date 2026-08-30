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
    : profile_id(std::move(profile_id_value)),
      run_id(std::move(run_id_value)),
      checkpoints(checkpoint_factory.checkpoints(profile_id)),
      lease(std::move(lease_value)),
      cancellation_requests_(cancellation_requests) {
    active_run = cancellation_requests.register_active_run({profile_id, run_id});
    cancellation_watch = cancellation_monitor.watch({profile_id, run_id}, cancellation);
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

RunExecutionContextCloseResult RunExecutionContext::close() {
    RunExecutionContextCloseResult result;
    if (closed_) {
        return result;
    }
    closed_ = true;

    close_cancellation_watch(result);
    release_event_sink();
    release_checkpoint_store();
    close_active_run(result);
    clear_cancellation_request(result);
    collect_target_session_failure(result);
    release_lease();
    report_close_failures(result);
    return result;
}

void RunExecutionContext::attach_event_sink(std::unique_ptr<IBackupRunEventSink> events) noexcept {
    events_ = std::move(events);
}

void RunExecutionContext::attach_target_session(std::unique_ptr<IMountedTargetSession> session) {
    target_session = std::move(session);
}

std::optional<TargetCleanupError> RunExecutionContext::close_target_session() noexcept {
    if (target_close_attempted_) {
        return target_close_error_;
    }
    target_close_attempted_ = true;
    if (target_session != nullptr) {
        target_close_error_ = target_session->close();
        target_session.reset();
    }
    return target_close_error_;
}

IBackupRunEventSink& RunExecutionContext::event_sink() const {
    if (events_ == nullptr) {
        throw std::logic_error("run execution context has no event sink");
    }
    return *events_;
}

void RunExecutionContext::close_cancellation_watch(RunExecutionContextCloseResult& result) {
    if (cancellation_watch == nullptr) {
        return;
    }
    try {
        if (std::optional<std::string> diagnostic = cancellation_watch->close()) {
            result.failures.push_back({RunExecutionContextCloseStage::CancellationWatch, std::move(*diagnostic)});
        }
    } catch (const std::exception& error) {
        result.failures.push_back({RunExecutionContextCloseStage::CancellationWatch, error.what()});
    } catch (...) {
        result.failures.push_back({RunExecutionContextCloseStage::CancellationWatch, "unknown cleanup failure"});
    }
    cancellation_watch.reset();
}

void RunExecutionContext::release_event_sink() noexcept {
    events_.reset();
}

void RunExecutionContext::release_checkpoint_store() noexcept {
    checkpoints.reset();
}

void RunExecutionContext::close_active_run(RunExecutionContextCloseResult& result) {
    if (active_run == nullptr) {
        return;
    }
    try {
        if (std::optional<std::string> diagnostic = active_run->close()) {
            result.failures.push_back({RunExecutionContextCloseStage::ActiveRun, std::move(*diagnostic)});
        }
    } catch (const std::exception& error) {
        result.failures.push_back({RunExecutionContextCloseStage::ActiveRun, error.what()});
    } catch (...) {
        result.failures.push_back({RunExecutionContextCloseStage::ActiveRun, "unknown cleanup failure"});
    }
    active_run.reset();
}

void RunExecutionContext::clear_cancellation_request(RunExecutionContextCloseResult& result) {
    try {
        cancellation_requests_.clear_cancel_request({profile_id, run_id});
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
    lease.reset();
}

void RunExecutionContext::report_close_failures(const RunExecutionContextCloseResult& result) const noexcept {
    for (const RunExecutionContextCloseFailure& failure : result.failures) {
        std::fputs("btrfs-backup: cleanup failed for profile ", stderr);
        std::fwrite(profile_id.value().data(), 1, profile_id.value().size(), stderr);
        std::fputs(", run ", stderr);
        std::fwrite(run_id.value().data(), 1, run_id.value().size(), stderr);
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
