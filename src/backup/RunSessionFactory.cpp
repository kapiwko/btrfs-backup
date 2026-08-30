// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/RunSessionFactory.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <variant>

namespace btrfsbackup::backup {
namespace {

BackupRunStatusDescription status_description(
    const btrfsbackup::config::Profile& profile,
    RuntimeTimePoint started_at
) {
    std::map<std::string, std::string> source_names;
    for (const btrfsbackup::config::ProfileSource& source : profile.sources) {
        source_names.emplace(source.id.value(), source.name);
    }
    return {
        .profile_name = profile.name,
        .source_count = static_cast<int>(std::count_if(
            profile.sources.begin(),
            profile.sources.end(),
            [](const btrfsbackup::config::ProfileSource& source) { return source.enabled; }
        )),
        .started_at = started_at,
        .source_names = std::move(source_names),
        .target_name = profile.target.mapper_name.value(),
    };
}

} // namespace

RunSessionFactory::RunSessionFactory(
    IBackupRunLeaseProvider& leases,
    IRunEventSinkFactory& event_sinks,
    ICheckpointStoreFactory& checkpoints,
    ICancellationRequestStore& cancellation_requests,
    ICancellationMonitor& cancellation_monitor
)
    : leases_(leases),
      event_sinks_(event_sinks),
      checkpoints_(checkpoints),
      cancellation_requests_(cancellation_requests),
      cancellation_monitor_(cancellation_monitor) {
}

BackupRunLeaseResult RunSessionFactory::try_acquire_lease(
    const btrfsbackup::config::Profile& profile
) {
    return leases_.try_acquire(profile);
}

CancellationRequestOutcome RunSessionFactory::request_cancel(
    const btrfsbackup::config::Profile& profile,
    const CancellationRequest& request
) {
    BackupRunLeaseResult lease = leases_.try_acquire(profile);
    return std::holds_alternative<BackupRunLeaseAcquired>(lease)
        ? CancellationRequestOutcome::StaleRun
        : cancellation_requests_.request_cancel(request);
}

std::unique_ptr<IBackupRunEventSink> RunSessionFactory::events(
    const btrfsbackup::config::LoadedProfile& loaded,
    const RunIdentity& identity
) {
    return event_sinks_.events(status_description(loaded.profile, identity.started_at));
}

std::unique_ptr<IBackupRunEventSink> RunSessionFactory::fallback_events(
    const ProfileId& profile_id,
    const RunIdentity& identity
) {
    return event_sinks_.events({
        .profile_name = std::string(profile_id.value()),
        .source_count = 0,
        .started_at = identity.started_at,
        .source_names = {},
        .target_name = {},
    });
}

std::unique_ptr<RunExecutionContext> RunSessionFactory::create_preparing(
    const btrfsbackup::config::LoadedProfile& loaded,
    const RunIdentity& identity,
    std::unique_ptr<IBackupRunLease> lease
) {
    return std::make_unique<RunExecutionContext>(
        loaded.profile.id,
        identity.run_id,
        std::move(lease),
        checkpoints_,
        cancellation_requests_,
        cancellation_monitor_
    );
}

} // namespace btrfsbackup::backup
