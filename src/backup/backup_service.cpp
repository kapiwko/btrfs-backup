// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_service.hpp>
#include <backup/run_execution_context.hpp>

#include <utility>

namespace btrfsbackup::backup {

namespace {

BackupRunStatusDescription status_description(
    const btrfsbackup::config::Profile& profile,
    const BackupRunPlan& plan,
    const std::string& started_at
) {
    std::map<std::string, std::string> source_names;
    for (const btrfsbackup::config::ProfileSource& source : profile.sources) {
        source_names.emplace(source.id.value(), source.name);
    }
    return {
        .profile_name = profile.name,
        .source_count = static_cast<int>(plan.sources.size()),
        .started_at = started_at,
        .source_names = std::move(source_names),
        .target_name = profile.target.mapper_name,
    };
}

} // namespace

BackupService::BackupService(
    btrfsbackup::config::IProfileRepository& profiles,
    btrfsbackup::config::ApplicationPaths application_paths,
    IMountInspector& mounts,
    ITargetManager& target_mounter,
    IBackupPlanner& planner,
    IBackupRunFactory& run_factory,
    IBackupRunLeaseProvider& leases,
    IRunStateRepository& state,
    ICancellationMonitor& cancellation_monitor,
    IClock& clock,
    IRunIdGenerator& run_ids
)
    : profiles_(profiles),
      application_paths_(std::move(application_paths)),
      mounts_(mounts),
      target_mounter_(target_mounter),
      planner_(planner),
      run_factory_(run_factory),
      leases_(leases),
      state_(state),
      cancellation_monitor_(cancellation_monitor),
      clock_(clock),
      run_ids_(run_ids) {
}

BackupRunPlan BackupService::prepare_plan(
    const btrfsbackup::config::Profile& profile,
    const RunId& run_id,
    const std::string& timestamp
) {
    target_mounter_.ensure_mounted(profile);
    return planner_.build(profile, mounts_.inspect(), application_paths_, run_id, timestamp);
}

BackupRunPlan BackupService::plan(const BackupRequest& request) {
    const std::string timestamp = clock_.snapshot_timestamp();
    const RunId run_id = run_ids_.generate(timestamp);
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(request.profile_id);
    return prepare_plan(loaded.profile, run_id, timestamp);
}

BackupExecutionResult BackupService::start(const BackupRequest& request) {
    const std::string timestamp = clock_.snapshot_timestamp();
    const RunId run_id = run_ids_.generate(timestamp);
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(request.profile_id);
    const btrfsbackup::config::Profile& profile = loaded.profile;

    BackupExecutionResult result{
        .plan = BackupRunPlan{
            .profile_id = profile.id,
            .run_id = run_id,
            .target_mount_point = {},
            .sources = {},
        },
        .outcome = BackupExecutionOutcome::Completed,
        .actions_completed = 0,
        .error_code = std::nullopt,
        .error_message = {},
    };

    BackupRunLeaseResult lease = leases_.try_acquire(profile);
    if (!lease.lease) {
        result.outcome = BackupExecutionOutcome::Busy;
        result.error_code = lease.error_code;
        result.error_message = std::move(lease.error_message);
        return result;
    }

    result.plan = prepare_plan(profile, run_id, timestamp);
    const std::string& fingerprint = loaded.fingerprint.value();
    if (request.validate_only) {
        result.outcome = BackupExecutionOutcome::Validated;
        return result;
    }

    const std::string today = clock_.local_date();
    if (!request.force && profile.settings.daily_limit && state_.last_success_matches(profile, today, fingerprint)) {
        state_.write_skipped(profile, run_id, timestamp, clock_.local_timestamp(), result.plan.sources.size());
        result.outcome = BackupExecutionOutcome::Skipped;
        return result;
    }

    state_.clear_cancel_request(request.profile_id);
    RunExecutionContext context(
        profile.id,
        run_id,
        std::move(lease.lease),
        state_,
        cancellation_monitor_,
        status_description(profile, result.plan, timestamp)
    );
    BackupRunExecutionResult execution = run_factory_.execute(
        result.plan,
        *context.events,
        *context.checkpoints,
        context.cancellation
    );
    state_.clear_cancel_request(request.profile_id);

    result.actions_completed = execution.actions_completed;
    result.outcome = execution.outcome == BackupRunExecutionOutcome::Completed
        ? BackupExecutionOutcome::Completed
        : BackupExecutionOutcome::Cancelled;
    if (execution.outcome == BackupRunExecutionOutcome::Completed) {
        state_.write_success(
            profile,
            run_id,
            today,
            clock_.local_timestamp(),
            fingerprint,
            result.plan.sources.size()
        );
    }
    return result;
}

CancelBackupResult BackupService::cancel(const ProfileId& profile_id) {
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(profile_id);
    state_.request_cancel(loaded.profile.id);
    return {.profile_id = loaded.profile.id, .cancel_requested = true};
}

} // namespace btrfsbackup::backup
