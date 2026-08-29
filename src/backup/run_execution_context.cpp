// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/run_execution_context.hpp>

#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

namespace btrfsbackup::backup {

namespace {

std::string_view close_stage_name(RunExecutionContextCloseStage stage) {
    switch (stage) {
    case RunExecutionContextCloseStage::CancellationWatch:
        return "cancellation-watch";
    case RunExecutionContextCloseStage::EventSink:
        return "event-sink";
    case RunExecutionContextCloseStage::CheckpointStore:
        return "checkpoint-store";
    case RunExecutionContextCloseStage::ActiveRun:
        return "active-run";
    case RunExecutionContextCloseStage::CancellationRequest:
        return "cancellation-request";
    case RunExecutionContextCloseStage::TargetSession:
        return "target-session";
    case RunExecutionContextCloseStage::Lease:
        return "lease";
    }
    return "unknown";
}

void record_exception(
    CloseResult& result,
    RunExecutionContextCloseStage stage,
    const std::exception& error
) {
    result.failures.push_back({stage, error.what()});
}

void record_unknown_exception(CloseResult& result, RunExecutionContextCloseStage stage) {
    result.failures.push_back({stage, "unknown cleanup failure"});
}

template <typename Resource>
void reset_resource(
    std::unique_ptr<Resource>& resource,
    RunExecutionContextCloseStage stage,
    CloseResult& result
) {
    try {
        resource.reset();
    } catch (const std::exception& error) {
        record_exception(result, stage, error);
    } catch (...) {
        record_unknown_exception(result, stage);
    }
}

template <typename Resource>
void close_resource(
    std::unique_ptr<Resource>& resource,
    RunExecutionContextCloseStage stage,
    CloseResult& result
) {
    if (resource == nullptr) {
        return;
    }
    try {
        if (std::optional<std::string> diagnostic = resource->close()) {
            result.failures.push_back({stage, std::move(*diagnostic)});
        }
    } catch (const std::exception& error) {
        record_exception(result, stage, error);
    } catch (...) {
        record_unknown_exception(result, stage);
    }
    reset_resource(resource, stage, result);
}

void write_diagnostics(const ProfileId& profile_id, const RunId& run_id, const CloseResult& result) noexcept {
    for (const RunExecutionContextCloseFailure& failure : result.failures) {
        std::clog << "btrfs-backup: cleanup failed for profile " << profile_id.value()
                  << ", run " << run_id.value() << ", stage " << close_stage_name(failure.stage)
                  << ": " << failure.message << '\n';
    }
}

} // namespace

RunExecutionContext::RunExecutionContext(
    ProfileId profile_id_value,
    RunId run_id_value,
    std::unique_ptr<IBackupRunEventSink>& events,
    std::unique_ptr<IBackupRunLease> lease_value,
    ICheckpointStoreFactory& checkpoint_factory,
    ICancellationRequestStore& cancellation_requests,
    ICancellationMonitor& cancellation_monitor
)
    : profile_id(std::move(profile_id_value)),
      run_id(std::move(run_id_value)),
      checkpoints(checkpoint_factory.checkpoints(profile_id)),
      lease(std::move(lease_value)),
      events_(events),
      cancellation_requests_(cancellation_requests) {
    active_run = cancellation_requests.register_active_run({profile_id, run_id});
    cancellation_watch = cancellation_monitor.watch({profile_id, run_id}, cancellation);
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

RunExecutionContext::~RunExecutionContext() noexcept {
    try {
        (void)close();
    } catch (const std::exception& error) {
        std::clog << "btrfs-backup: run context cleanup failed before diagnostics could be completed: "
                  << error.what() << '\n';
    } catch (...) {
        std::clog << "btrfs-backup: run context cleanup failed with an unknown error\n";
    }
}

CloseResult RunExecutionContext::close() {
    CloseResult result;
    if (closed_) {
        return result;
    }
    closed_ = true;

    close_resource(cancellation_watch, RunExecutionContextCloseStage::CancellationWatch, result);
    reset_resource(events_, RunExecutionContextCloseStage::EventSink, result);
    reset_resource(checkpoints, RunExecutionContextCloseStage::CheckpointStore, result);
    close_resource(active_run, RunExecutionContextCloseStage::ActiveRun, result);
    try {
        cancellation_requests_.clear_cancel_request({profile_id, run_id});
    } catch (const std::exception& error) {
        record_exception(result, RunExecutionContextCloseStage::CancellationRequest, error);
    } catch (...) {
        record_unknown_exception(result, RunExecutionContextCloseStage::CancellationRequest);
    }
    if (std::optional<TargetCleanupError> error = close_target_session()) {
        result.failures.push_back({RunExecutionContextCloseStage::TargetSession, error->message});
    }
    reset_resource(lease, RunExecutionContextCloseStage::Lease, result);

    write_diagnostics(profile_id, run_id, result);
    return result;
}

} // namespace btrfsbackup::backup
