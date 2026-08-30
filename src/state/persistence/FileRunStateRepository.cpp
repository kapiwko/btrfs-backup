// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/persistence/FileRunStateRepository.hpp>

#include <optional>
#include <string>
#include <utility>

#include <state/cancellation/FileActiveRunRegistration.hpp>
#include <state/persistence/JsonFileBackupRunCheckpointStore.hpp>
#include <state/query/RunHistory.hpp>
#include <state/query/RunState.hpp>
#include <state/projection/RunStatusProjection.hpp>
#include <state/persistence/StatusWriter.hpp>
#include <core/RuntimeTime.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::state {

FileRunStateRepository::FileRunStateRepository(
    btrfsbackup::config::ApplicationPaths paths,
    IPersistentDocumentOperations& files
)
    : paths_(std::move(paths)), files_(files) {
}

fs::path FileRunStateRepository::state_dir(const ProfileId& profile_id) const {
    return btrfsbackup::config::profile_state_dir(paths_, std::string(profile_id.value()));
}

bool FileRunStateRepository::last_success_matches(
    const btrfsbackup::config::Profile& profile,
    LocalDate date,
    const btrfsbackup::config::ConfigurationFingerprint& fingerprint
) const {
    return btrfsbackup::state::last_success_matches(
        state_dir(profile.id),
        date,
        profile.target.luks_uuid,
        fingerprint
    );
}

void FileRunStateRepository::write_skipped(
    const btrfsbackup::config::Profile& profile,
    const RunId& run_id,
    RuntimeTimePoint started_at,
    RuntimeTimePoint finished_at,
    std::size_t source_count
) {
    RunStatus status{
        .profile_id = profile.id,
        .profile_name = profile.name,
        .run_id = run_id,
        .state = RunState::Skipped,
        .phase = RunPhase::Skipped,
        .message = "A successful backup already exists for today; no new snapshot was created.",
        .current_source_name = {},
        .target_name = profile.target.mapper_name.value(),
        .source_count = static_cast<int>(source_count),
        .started_at = started_at,
        .updated_at = finished_at,
        .finished_at = finished_at,
        .error = std::nullopt,
        .details = {},
        .can_cancel = false,
        .progress = {},
        .exit_code = 0,
    };
    write_current_status(files_, paths_.status_root, status);
    write_history_entry(files_, paths_.history_root, status);
}

void FileRunStateRepository::write_success(
    const btrfsbackup::config::Profile& profile,
    const RunId& run_id,
    LocalDate date,
    RuntimeTimePoint timestamp,
    const btrfsbackup::config::ConfigurationFingerprint& fingerprint,
    std::size_t source_count
) {
    write_success_state(
        files_,
        state_dir(profile.id),
        SuccessState{
            .date = date,
            .timestamp = timestamp,
            .run_id = run_id,
            .profile_id = profile.id,
            .profile_name = profile.name,
            .source_count = static_cast<int>(source_count),
            .target_luks_uuid = profile.target.luks_uuid,
            .config_fingerprint = fingerprint,
        }
    );
}

std::unique_ptr<btrfsbackup::backup::IBackupRunCheckpointStore> FileRunStateRepository::checkpoints(const ProfileId& profile_id) {
    return std::make_unique<JsonFileBackupRunCheckpointStore>(files_, state_dir(profile_id));
}

std::unique_ptr<btrfsbackup::backup::IBackupRunEventSink> FileRunStateRepository::events(btrfsbackup::backup::BackupRunStatusDescription description) {
    return std::make_unique<RunStatusProjection>(files_, BackupRunStatusContext{
                                                             .status_root = paths_.status_root,
                                                             .history_root = paths_.history_root,
                                                             .profile_name = std::move(description.profile_name),
                                                             .source_count = description.source_count,
                                                             .started_at = description.started_at,
                                                             .source_names = std::move(description.source_names),
                                                             .target_name = std::move(description.target_name),
                                                         });
}

std::unique_ptr<btrfsbackup::backup::IActiveRunRegistration> FileRunStateRepository::register_active_run(
    const btrfsbackup::backup::CancellationRequest& request
) {
    const fs::path directory = state_dir(request.profile_id);
    auto registration = std::make_unique<FileActiveRunRegistration>(files_, directory, request.run_id);
    btrfsbackup::state::clear_cancel_request(files_, directory);
    write_active_run(files_, directory, request.run_id);
    return registration;
}

btrfsbackup::backup::CancellationRequestOutcome FileRunStateRepository::request_cancel(
    const btrfsbackup::backup::CancellationRequest& request
) {
    const fs::path directory = state_dir(request.profile_id);
    const std::optional<RunId> active = active_run(directory);
    if (!active.has_value()) {
        return btrfsbackup::backup::CancellationRequestOutcome::StaleRun;
    }
    if (*active != request.run_id) {
        return btrfsbackup::backup::CancellationRequestOutcome::RunMismatch;
    }

    write_cancel_request(files_, directory, request.run_id);
    const std::optional<RunId> confirmed = active_run(directory);
    if (!confirmed.has_value()) {
        clear_cancel_request(request);
        return btrfsbackup::backup::CancellationRequestOutcome::StaleRun;
    }
    if (*confirmed != request.run_id) {
        clear_cancel_request(request);
        return btrfsbackup::backup::CancellationRequestOutcome::RunMismatch;
    }
    return btrfsbackup::backup::CancellationRequestOutcome::Accepted;
}

bool FileRunStateRepository::cancel_requested(
    const btrfsbackup::backup::CancellationRequest& request
) const {
    return btrfsbackup::state::cancel_requested(state_dir(request.profile_id), request.run_id);
}

void FileRunStateRepository::clear_cancel_request(
    const btrfsbackup::backup::CancellationRequest& request
) {
    btrfsbackup::state::clear_cancel_request(files_, state_dir(request.profile_id), request.run_id);
}

} // namespace btrfsbackup::state
