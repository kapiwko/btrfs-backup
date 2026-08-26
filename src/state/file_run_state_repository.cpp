// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/file_run_state_repository.hpp>

#include <chrono>
#include <thread>
#include <utility>

#include <state/run_checkpoint_store.hpp>
#include <state/run_history.hpp>
#include <state/run_state.hpp>
#include <state/run_status_projection.hpp>
#include <state/status_writer.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

class PollingCancellationWatch final : public ICancellationWatch {
  public:
    PollingCancellationWatch(
        IRunStateRepository& state,
        ProfileId profile_id,
        CancellationToken& cancellation
    )
        : state_(state),
          profile_id_(std::move(profile_id)),
          cancellation_(cancellation),
          worker_([this](std::stop_token stop) { run(stop); }) {
    }

    PollingCancellationWatch(const PollingCancellationWatch&) = delete;
    PollingCancellationWatch& operator=(const PollingCancellationWatch&) = delete;

    ~PollingCancellationWatch() override = default;

  private:
    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            if (state_.cancel_requested(profile_id_)) {
                cancellation_.request_cancel();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    IRunStateRepository& state_;
    ProfileId profile_id_;
    CancellationToken& cancellation_;
    std::jthread worker_;
};

} // namespace

FileRunStateRepository::FileRunStateRepository(ApplicationPaths paths, IDurableFileOperations& files)
    : paths_(std::move(paths)), files_(files) {
}

fs::path FileRunStateRepository::state_dir(const ProfileId& profile_id) const {
    return profile_state_dir(paths_, std::string(profile_id.value()));
}

bool FileRunStateRepository::last_success_matches(
    const Profile& profile,
    const std::string& date,
    const std::string& fingerprint
) const {
    return btrfsbackup::last_success_matches(
        state_dir(profile.id),
        date,
        profile.target.luks_uuid,
        fingerprint
    );
}

void FileRunStateRepository::write_skipped(
    const Profile& profile,
    const RunId& run_id,
    const std::string& started_at,
    const std::string& finished_at,
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
        .target_name = profile.target.mapper_name,
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
    const Profile& profile,
    const RunId& run_id,
    const std::string& date,
    const std::string& timestamp,
    const std::string& fingerprint,
    std::size_t source_count
) {
    write_success_state(
        files_,
        state_dir(profile.id),
        SuccessState{
            .date = date,
            .timestamp = timestamp,
            .run_id = std::string(run_id.value()),
            .profile_id = std::string(profile.id.value()),
            .profile_name = profile.name,
            .source_count = static_cast<int>(source_count),
            .target_luks_uuid = profile.target.luks_uuid,
            .config_fingerprint = fingerprint,
        }
    );
}

std::unique_ptr<IBackupRunCheckpointStore> FileRunStateRepository::checkpoints(const ProfileId& profile_id) {
    return std::make_unique<JsonFileBackupRunCheckpointStore>(files_, state_dir(profile_id));
}

std::unique_ptr<IBackupRunEventSink> FileRunStateRepository::events(BackupRunStatusDescription description) {
    return std::make_unique<RunStatusProjection>(files_, BackupRunStatusContext{
                                                             .status_root = paths_.status_root,
                                                             .history_root = paths_.history_root,
                                                             .profile_name = std::move(description.profile_name),
                                                             .source_count = description.source_count,
                                                             .started_at = std::move(description.started_at),
                                                             .source_names = std::move(description.source_names),
                                                             .target_name = std::move(description.target_name),
                                                         });
}

void FileRunStateRepository::request_cancel(const ProfileId& profile_id) {
    write_cancel_request(files_, state_dir(profile_id));
}

bool FileRunStateRepository::cancel_requested(const ProfileId& profile_id) const {
    return btrfsbackup::cancel_requested(state_dir(profile_id));
}

void FileRunStateRepository::clear_cancel_request(const ProfileId& profile_id) {
    btrfsbackup::clear_cancel_request(files_, state_dir(profile_id));
}

FileCancellationMonitor::FileCancellationMonitor(IRunStateRepository& state) : state_(state) {
}

std::unique_ptr<ICancellationWatch> FileCancellationMonitor::watch(
    const ProfileId& profile_id,
    CancellationToken& cancellation
) {
    return std::make_unique<PollingCancellationWatch>(state_, profile_id, cancellation);
}

} // namespace btrfsbackup
