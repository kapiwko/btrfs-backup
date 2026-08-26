// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_service.hpp>

#include <utility>

namespace btrfsbackup {

namespace {

BackupRunStatusDescription status_description(
    const Profile& profile,
    const BackupRunPlan& plan,
    const std::string& started_at
) {
    std::map<std::string, std::string> source_names;
    for (const ProfileSource& source : profile.sources) {
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
    IProfileRepository& profiles,
    IMountInspector& mounts,
    ITargetManager& target_manager,
    IBackupPlanner& planner,
    IBackupRunFactory& run_factory,
    IBackupLockManager& locks,
    IRunStateRepository& state,
    ICancellationMonitor& cancellation_monitor,
    IClock& clock,
    IRunIdGenerator& run_ids,
    CancellationToken& cancellation
)
    : profiles_(profiles),
      mounts_(mounts),
      target_manager_(target_manager),
      planner_(planner),
      run_factory_(run_factory),
      locks_(locks),
      state_(state),
      cancellation_monitor_(cancellation_monitor),
      clock_(clock),
      run_ids_(run_ids),
      cancellation_(cancellation) {
}

BackupRunPlan BackupService::prepare_plan(
    const Profile& profile,
    const RunId& run_id,
    const std::string& timestamp
) {
    target_manager_.ensure_mounted(profile);
    return planner_.build(profile, mounts_.inspect(), profiles_.application_paths(), run_id, timestamp);
}

BackupRunPlan BackupService::plan(const BackupRequest& request) {
    const std::string timestamp = clock_.snapshot_timestamp();
    const RunId run_id = run_ids_.generate(timestamp);
    const Profile profile = profiles_.get(request.profile_id);
    return prepare_plan(profile, run_id, timestamp);
}

BackupExecutionResult BackupService::start(const BackupRequest& request) {
    const std::string timestamp = clock_.snapshot_timestamp();
    const RunId run_id = run_ids_.generate(timestamp);
    const Profile profile = profiles_.get(request.profile_id);

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

    BackupRunLeaseResult lease = locks_.try_acquire(profile);
    if (!lease.lease) {
        result.outcome = BackupExecutionOutcome::Busy;
        result.error_code = lease.error_code;
        result.error_message = std::move(lease.error_message);
        return result;
    }

    result.plan = prepare_plan(profile, run_id, timestamp);
    const std::string fingerprint = profiles_.fingerprint(profile);
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
    auto checkpoints = state_.checkpoints(request.profile_id);
    auto events = state_.events(status_description(profile, result.plan, timestamp));
    auto cancel_monitor = cancellation_monitor_.watch(request.profile_id, cancellation_);
    BackupRunExecutionResult execution = run_factory_.execute(
        result.plan,
        *events,
        *checkpoints,
        cancellation_
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
    const Profile profile = profiles_.get(profile_id);
    state_.request_cancel(profile.id);
    return {.profile_id = profile.id, .cancel_requested = true};
}

} // namespace btrfsbackup
