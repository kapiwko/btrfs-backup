// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_service_adapters.hpp>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

#include <backup/backup_run.hpp>
#include <backup/pending_recovery_plan.hpp>
#include <backup/target_mount_validation.hpp>
#include <core/errors.hpp>
#include <config/profile_loader.hpp>
#include <platform/linux/file_lock.hpp>
#include <platform/linux/safe_directory_root.hpp>
#include <state/run_checkpoint_store.hpp>
#include <state/run_status_projection.hpp>
#include <state/config_fingerprint.hpp>
#include <state/run_state.hpp>
#include <state/status_writer.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

std::string format_time(const char* format, bool utc) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    if (utc) {
        gmtime_r(&time, &tm);
    } else {
        localtime_r(&time, &tm);
    }
    std::ostringstream out;
    out << std::put_time(&tm, format);
    return out.str();
}

class FileBackupRunLease final : public IBackupRunLease {
  public:
    FileBackupRunLease(FileLock profile_lock, FileLock target_lock)
        : profile_lock_(std::move(profile_lock)), target_lock_(std::move(target_lock)) {
    }

  private:
    FileLock profile_lock_;
    FileLock target_lock_;
};

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

FileProfileRepository::FileProfileRepository(fs::path config_root)
    : FileProfileRepository(config_root, ApplicationConfig::load(config_root)) {
}

FileProfileRepository::FileProfileRepository(fs::path config_root, ApplicationConfig application_config)
    : config_root_(std::move(config_root)), application_config_(std::move(application_config)) {
}

Profile FileProfileRepository::get(const ProfileId& profile_id) const {
    return load_profile_by_id(config_root_, std::string(profile_id.value()));
}

const ApplicationPaths& FileProfileRepository::application_paths() const {
    return application_config_.paths();
}

std::string FileProfileRepository::fingerprint(const Profile& profile) const {
    return compute_config_fingerprint(
        "2.0.0",
        config_root_ / "profiles" / profile.id.value() / "profile.json",
        {}
    );
}

SystemdTargetMounter::SystemdTargetMounter(IMountInspector& mounts, ICommandRunner& commands)
    : mounts_(mounts), commands_(commands) {
}

void SystemdTargetMounter::ensure_mounted(const Profile& profile) {
    if (mount_at(mounts_.inspect(), profile.target.mount_point).has_value()) {
        return;
    }
    if (profile.target.mount_unit.empty()) {
        throw ValidationError("target.mountUnit is required to mount backup target");
    }
    const CommandResult result = commands_.run({"systemctl", "start", profile.target.mount_unit});
    if (result.exit_code != 0) {
        throw ValidationError("could not start target mount unit " + profile.target.mount_unit);
    }
}

DefaultBackupPlanner::DefaultBackupPlanner(SnapshotMetadataReader metadata_reader, bool secure_paths)
    : metadata_reader_(std::move(metadata_reader)), secure_paths_(secure_paths) {
}

BackupRunPlan DefaultBackupPlanner::build(
    const Profile& profile,
    const std::vector<MountEntry>& mounts,
    const ApplicationPaths& paths,
    const RunId& run_id,
    const std::string& snapshot_timestamp
) const {
    validate_target_mount(profile, mounts);
    SnapshotInventoryBySource local_inventory;
    SnapshotInventoryBySource remote_inventory;
    PendingMarkerBySource pending_markers;
    PendingSnapshotBySource pending_snapshots;
    const fs::path profile_state = profile_state_dir(paths, std::string(profile.id.value()));
    std::optional<SafeDirectoryRoot> local_root;
    std::optional<SafeDirectoryRoot> target_root;
    if (secure_paths_) {
        local_root.emplace("/");
        target_root.emplace(profile.target.mount_point);
    }

    for (const ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        const std::string source_id{source.id.value()};
        const fs::path remote_dir = fs::path(profile.paths.remote_root) / source.remote_subdir;
        if (secure_paths_) {
            if (local_root->exists(source.local_snapshot_dir)) {
                SafeDirectoryHandle local = local_root->open_directory(source.local_snapshot_dir);
                local_inventory[source_id] = list_snapshot_inventory_at(
                    local.proc_path(),
                    source.local_snapshot_dir,
                    source_id,
                    SnapshotSide::Local,
                    [&](const fs::path& path) {
                        return metadata_reader_(local_root->open_directory(
                                                              fs::path(source.local_snapshot_dir) / path.filename()
                        )
                                                    .proc_path());
                    }
                );
            }
            if (target_root->exists(remote_dir)) {
                SafeDirectoryHandle remote = target_root->open_directory(remote_dir);
                remote_inventory[source_id] = list_snapshot_inventory_at(
                    remote.proc_path(),
                    remote_dir,
                    source_id,
                    SnapshotSide::Remote,
                    [&](const fs::path& path) {
                        return metadata_reader_(target_root->open_directory(remote_dir / path.filename()).proc_path());
                    }
                );
            }
        } else {
            local_inventory[source_id] = list_snapshot_inventory(
                source.local_snapshot_dir,
                source_id,
                SnapshotSide::Local,
                metadata_reader_
            );
            remote_inventory[source_id] = list_snapshot_inventory(
                remote_dir,
                source_id,
                SnapshotSide::Remote,
                metadata_reader_
            );
        }

        const std::optional<PendingMarker> marker = read_pending_marker_if_exists(profile_state, source_id);
        pending_markers[source_id] = marker;
        if (marker.has_value()) {
            if (secure_paths_ && local_root->exists(marker->local_snapshot_path)) {
                pending_snapshots[source_id] = metadata_reader_(
                    local_root->open_directory(marker->local_snapshot_path).proc_path()
                );
            } else if (secure_paths_) {
                pending_snapshots[source_id] = std::nullopt;
            } else {
                pending_snapshots[source_id] = metadata_reader_(marker->local_snapshot_path);
            }
        }
    }

    return build_backup_run_plan(
        profile,
        mounts,
        local_inventory,
        remote_inventory,
        pending_markers,
        pending_snapshots,
        profile_state,
        run_id,
        snapshot_timestamp
    );
}

DefaultBackupRunFactory::DefaultBackupRunFactory(
    IBackupRunActionHandler& action_handler,
    ITransferPipeline& transfers,
    bool pin_transfer_paths
)
    : action_handler_(action_handler), transfers_(transfers), pin_transfer_paths_(pin_transfer_paths) {
}

BackupRunExecutionResult DefaultBackupRunFactory::execute(
    BackupRunPlan plan,
    IBackupRunEventSink& events,
    IBackupRunCheckpointStore& checkpoints,
    CancellationToken& cancellation
) {
    if (!pin_transfer_paths_) {
        plan.target_mount_point.clear();
    }
    ThreadedAsyncTransferPipeline async_transfers(transfers_);
    BackupRun run(std::move(plan), action_handler_, async_transfers, checkpoints);
    return run.execute(events, cancellation);
}

FileBackupRunLeaseProvider::FileBackupRunLeaseProvider(fs::path lock_root) : lock_root_(std::move(lock_root)) {
}

BackupRunLeaseResult FileBackupRunLeaseProvider::try_acquire(const Profile& profile) {
    FileLock profile_lock(profile_lock_path(lock_root_, std::string(profile.id.value())));
    if (!profile_lock.try_acquire()) {
        return {
            .lease = nullptr,
            .error_code = ErrorCode::RunnerProfileBusy,
            .error_message = "Another runner is already active for profile " + std::string(profile.id.value()) + ".",
        };
    }
    FileLock target_lock(target_lock_path(lock_root_, profile.target.luks_uuid));
    if (!target_lock.try_acquire()) {
        return {
            .lease = nullptr,
            .error_code = ErrorCode::RunnerTargetBusy,
            .error_message = "Another operation is already active for target LUKS UUID " + profile.target.luks_uuid + ".",
        };
    }
    return {
        .lease = std::make_unique<FileBackupRunLease>(std::move(profile_lock), std::move(target_lock)),
        .error_code = std::nullopt,
        .error_message = {},
    };
}

FileRunStateRepository::FileRunStateRepository(ApplicationPaths paths) : paths_(std::move(paths)) {
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
    write_current_status(paths_.status_root, status);
    write_history_entry(paths_.history_root, status);
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
    return std::make_unique<JsonFileBackupRunCheckpointStore>(state_dir(profile_id));
}

std::unique_ptr<IBackupRunEventSink> FileRunStateRepository::events(BackupRunStatusDescription description) {
    return std::make_unique<RunStatusProjection>(BackupRunStatusContext{
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
    write_cancel_request(state_dir(profile_id));
}

bool FileRunStateRepository::cancel_requested(const ProfileId& profile_id) const {
    return btrfsbackup::cancel_requested(state_dir(profile_id));
}

void FileRunStateRepository::clear_cancel_request(const ProfileId& profile_id) {
    btrfsbackup::clear_cancel_request(state_dir(profile_id));
}

FileCancellationMonitor::FileCancellationMonitor(IRunStateRepository& state) : state_(state) {
}

std::unique_ptr<ICancellationWatch> FileCancellationMonitor::watch(
    const ProfileId& profile_id,
    CancellationToken& cancellation
) {
    return std::make_unique<PollingCancellationWatch>(state_, profile_id, cancellation);
}

std::string SystemClock::snapshot_timestamp() const {
    return format_time("%Y-%m-%dT%H%M%SZ", true);
}

std::string SystemClock::local_date() const {
    return format_time("%Y-%m-%d", false);
}

std::string SystemClock::local_timestamp() const {
    return format_time("%Y-%m-%dT%H:%M:%S%z", false);
}

RunId TimestampRunIdGenerator::generate(const std::string& snapshot_timestamp) {
    std::string compact;
    for (const char character : snapshot_timestamp) {
        if (character != '-' && character != ':') {
            compact.push_back(character);
        }
    }
    return RunId{compact + "-shadow"};
}

} // namespace btrfsbackup
