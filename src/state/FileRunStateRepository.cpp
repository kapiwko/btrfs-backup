// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/FileRunStateRepository.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <state/JsonFileBackupRunCheckpointStore.hpp>
#include <state/RunHistory.hpp>
#include <state/RunState.hpp>
#include <state/RunStatusProjection.hpp>
#include <state/StatusWriter.hpp>
#include <core/RuntimeTime.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::state {

namespace {

class PollingCancellationWatch final : public btrfsbackup::backup::ICancellationWatch {
  public:
    PollingCancellationWatch(
        btrfsbackup::backup::ICancellationRequestStore& requests,
        btrfsbackup::backup::CancellationRequest request,
        CancellationToken& cancellation
    )
        : requests_(requests),
          request_(std::move(request)),
          cancellation_(cancellation),
          worker_([this](std::stop_token stop) { run(stop); }) {
    }

    PollingCancellationWatch(const PollingCancellationWatch&) = delete;
    PollingCancellationWatch& operator=(const PollingCancellationWatch&) = delete;

    ~PollingCancellationWatch() override {
        try {
            if (std::optional<std::string> diagnostic = close()) {
                std::clog << "btrfs-backup: cancellation watch cleanup failed: " << *diagnostic << '\n';
            }
        } catch (const std::exception& error) {
            std::clog << "btrfs-backup: cancellation watch cleanup failed: " << error.what() << '\n';
        } catch (...) {
            std::clog << "btrfs-backup: cancellation watch cleanup failed with an unknown error\n";
        }
    }

    std::optional<std::string> close() override {
        if (closed_) {
            return std::nullopt;
        }
        closed_ = true;
        worker_.request_stop();
        try {
            if (worker_.joinable()) {
                worker_.join();
            }
            return std::nullopt;
        } catch (const std::exception& error) {
            return error.what();
        } catch (...) {
            return "unknown cancellation watch cleanup failure";
        }
    }

  private:
    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            if (requests_.cancel_requested(request_)) {
                cancellation_.request_cancel();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    btrfsbackup::backup::ICancellationRequestStore& requests_;
    btrfsbackup::backup::CancellationRequest request_;
    CancellationToken& cancellation_;
    std::jthread worker_;
    bool closed_ = false;
};

class FileActiveRunRegistration final : public btrfsbackup::backup::IActiveRunRegistration {
  public:
    FileActiveRunRegistration(
        IDurableDocumentRemover& files,
        fs::path profile_state_dir,
        RunId run_id
    )
        : files_(files), profile_state_dir_(std::move(profile_state_dir)), run_id_(std::move(run_id)) {
    }

    ~FileActiveRunRegistration() override {
        try {
            if (std::optional<std::string> diagnostic = close()) {
                std::clog << "btrfs-backup: active run cleanup failed: " << *diagnostic << '\n';
            }
        } catch (const std::exception& error) {
            std::clog << "btrfs-backup: active run cleanup failed: " << error.what() << '\n';
        } catch (...) {
            std::clog << "btrfs-backup: active run cleanup failed with an unknown error\n";
        }
    }

    std::optional<std::string> close() override {
        if (closed_) {
            return std::nullopt;
        }
        closed_ = true;
        try {
            btrfsbackup::state::clear_active_run(files_, profile_state_dir_, run_id_);
            return std::nullopt;
        } catch (const std::exception& error) {
            return error.what();
        } catch (...) {
            return "unknown active run cleanup failure";
        }
    }

  private:
    IDurableDocumentRemover& files_;
    fs::path profile_state_dir_;
    RunId run_id_;
    bool closed_ = false;
};

} // namespace

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
    const std::string& fingerprint
) const {
    return btrfsbackup::state::last_success_matches(
        state_dir(profile.id),
        format_local_date(date),
        profile.target.luks_uuid.value(),
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
    const std::string& fingerprint,
    std::size_t source_count
) {
    write_success_state(
        files_,
        state_dir(profile.id),
        SuccessState{
            .date = format_local_date(date),
            .timestamp = format_local_timestamp(timestamp),
            .run_id = std::string(run_id.value()),
            .profile_id = std::string(profile.id.value()),
            .profile_name = profile.name,
            .source_count = static_cast<int>(source_count),
            .target_luks_uuid = profile.target.luks_uuid.value(),
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

FileCancellationMonitor::FileCancellationMonitor(
    btrfsbackup::backup::ICancellationRequestStore& requests
)
    : requests_(requests) {
}

std::unique_ptr<btrfsbackup::backup::ICancellationWatch> FileCancellationMonitor::watch(
    const btrfsbackup::backup::CancellationRequest& request,
    CancellationToken& cancellation
) {
    return std::make_unique<PollingCancellationWatch>(requests_, request, cancellation);
}

} // namespace btrfsbackup::state
