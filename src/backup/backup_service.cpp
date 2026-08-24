#include <backup/backup_service.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <platform/linux/command_runner.hpp>
#include <platform/linux/filesystem.hpp>
#include <backup/backup_run_action_effects.hpp>
#include <backup/backup_run.hpp>
#include <state/backup_run_persistence.hpp>
#include <backup/pending_recovery_plan.hpp>
#include <state/status_writer.hpp>
#include <state/config_fingerprint.hpp>
#include <config/errors.hpp>
#include <config/profile.hpp>
#include <platform/linux/btrfs_operations.hpp>
#include <platform/linux/file_lock.hpp>
#include <platform/linux/mount_info.hpp>
#include <platform/linux/process.hpp>
#include <config/profile_loader.hpp>
#include <state/run_state.hpp>
#include <platform/linux/safe_directory_root.hpp>
#include <backup/target_mount_validation.hpp>

namespace fs = std::filesystem;

namespace {

std::string current_utc_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H%M%SZ");
    return out.str();
}

std::string current_local_date() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d");
    return out.str();
}

std::string current_local_iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S%z");
    return out.str();
}

std::string compact_timestamp(const std::string& timestamp) {
    std::string result;
    for (char ch : timestamp) {
        if (ch != '-' && ch != ':') {
            result.push_back(ch);
        }
    }
    return result;
}

btrfsbackup::BackupRequest normalize_request(btrfsbackup::BackupRequest request) {
    if (request.timestamp.empty()) {
        request.timestamp = current_utc_timestamp();
    }
    if (request.today.empty()) {
        request.today = current_local_date();
    }
    if (request.run_id.value.empty()) {
        request.run_id = btrfsbackup::RunId{compact_timestamp(request.timestamp) + "-shadow"};
    }
    return request;
}

class CancelRequestMonitor {
public:
    CancelRequestMonitor(const fs::path& profile_state_dir, btrfsbackup::CancellationToken& cancellation)
        : profile_state_dir_(profile_state_dir),
          cancellation_(cancellation),
          worker_([this] { run(); }) {
    }

    CancelRequestMonitor(const CancelRequestMonitor&) = delete;
    CancelRequestMonitor& operator=(const CancelRequestMonitor&) = delete;

    ~CancelRequestMonitor() {
        stop_.store(true);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void run() {
        while (!stop_.load()) {
            if (btrfsbackup::cancel_requested(profile_state_dir_)) {
                cancellation_.request_cancel();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    fs::path profile_state_dir_;
    btrfsbackup::CancellationToken& cancellation_;
    std::atomic_bool stop_ = false;
    std::thread worker_;
};

std::vector<btrfsbackup::MountEntry> read_mounts(const btrfsbackup::BackupRequest& request) {
    return request.mount_uuid_overrides.empty()
        ? btrfsbackup::read_mount_table(request.mountinfo)
        : btrfsbackup::read_mount_table(request.mountinfo, [&request](const std::string& source) {
              auto found = request.mount_uuid_overrides.find(source);
              if (found != request.mount_uuid_overrides.end()) {
                  return found->second;
              }
              return btrfsbackup::blkid_filesystem_uuid(source);
          });
}

void ensure_target_mounted(const btrfsbackup::Profile& profile, const btrfsbackup::BackupRequest& request) {
    if (btrfsbackup::mount_at(read_mounts(request), profile.target.mount_point).has_value()) {
        return;
    }
    if (profile.target.mount_unit.empty()) {
        throw btrfsbackup::ValidationError("target.mountUnit is required to mount backup target");
    }
    btrfsbackup::CommandResult result = btrfsbackup::run_command({"systemctl", "start", profile.target.mount_unit});
    if (result.exit_code != 0) {
        throw btrfsbackup::ValidationError("could not start target mount unit " + profile.target.mount_unit);
    }
}

btrfsbackup::BackupRunPlan build_plan(
    const btrfsbackup::BackupRequest& request,
    const btrfsbackup::Profile& profile,
    const btrfsbackup::ApplicationPaths& application_paths,
    const btrfsbackup::SnapshotMetadataReader& metadata_reader,
    bool secure_paths
) {
    std::vector<btrfsbackup::MountEntry> mounts = read_mounts(request);
    btrfsbackup::validate_target_mount(profile, mounts);

    btrfsbackup::SnapshotInventoryBySource local_inventory;
    btrfsbackup::SnapshotInventoryBySource remote_inventory;
    btrfsbackup::PendingMarkerBySource pending_markers;
    btrfsbackup::PendingSnapshotBySource pending_snapshots;
    const fs::path state_dir = btrfsbackup::profile_state_dir(application_paths, profile.id);
    std::optional<btrfsbackup::SafeDirectoryRoot> local_root;
    std::optional<btrfsbackup::SafeDirectoryRoot> target_root;
    if (secure_paths) {
        local_root.emplace("/");
        target_root.emplace(profile.target.mount_point);
    }

    for (const btrfsbackup::ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        fs::path remote_dir = fs::path(profile.paths.remote_root) / source.remote_subdir;
        if (secure_paths) {
            if (local_root->exists(source.local_snapshot_dir)) {
                btrfsbackup::SafeDirectoryHandle local = local_root->open_directory(source.local_snapshot_dir);
                local_inventory[source.id] = btrfsbackup::list_snapshot_inventory_at(
                    local.proc_path(),
                    source.local_snapshot_dir,
                    source.id,
                    btrfsbackup::SnapshotSide::Local,
                    [&](const fs::path& scan_path) {
                        btrfsbackup::SafeDirectoryHandle snapshot = local_root->open_directory(
                            fs::path(source.local_snapshot_dir) / scan_path.filename()
                        );
                        return metadata_reader(snapshot.proc_path());
                    }
                );
            }
            if (target_root->exists(remote_dir)) {
                btrfsbackup::SafeDirectoryHandle remote = target_root->open_directory(remote_dir);
                remote_inventory[source.id] = btrfsbackup::list_snapshot_inventory_at(
                    remote.proc_path(),
                    remote_dir,
                    source.id,
                    btrfsbackup::SnapshotSide::Remote,
                    [&](const fs::path& scan_path) {
                        btrfsbackup::SafeDirectoryHandle snapshot = target_root->open_directory(
                            remote_dir / scan_path.filename()
                        );
                        return metadata_reader(snapshot.proc_path());
                    }
                );
            }
        } else {
            local_inventory[source.id] = btrfsbackup::list_snapshot_inventory(
                source.local_snapshot_dir,
                source.id,
                btrfsbackup::SnapshotSide::Local,
                metadata_reader
            );
            remote_inventory[source.id] = btrfsbackup::list_snapshot_inventory(
                remote_dir,
                source.id,
                btrfsbackup::SnapshotSide::Remote,
                metadata_reader
            );
        }

        std::optional<btrfsbackup::PendingMarker> marker =
            btrfsbackup::read_pending_marker_if_exists(state_dir, source.id);
        pending_markers[source.id] = marker;
        if (marker.has_value()) {
            if (secure_paths) {
                if (local_root->exists(marker->local_snapshot_path)) {
                    btrfsbackup::SafeDirectoryHandle pending =
                        local_root->open_directory(marker->local_snapshot_path);
                    pending_snapshots[source.id] = metadata_reader(pending.proc_path());
                } else {
                    pending_snapshots[source.id] = std::nullopt;
                }
            } else {
                pending_snapshots[source.id] = metadata_reader(marker->local_snapshot_path);
            }
        }
    }

    return btrfsbackup::build_backup_run_plan(
        profile,
        mounts,
        local_inventory,
        remote_inventory,
        pending_markers,
        pending_snapshots,
        state_dir,
        request.run_id,
        request.timestamp
    );
}

fs::path profile_json_path(const fs::path& profile_config_dir, const std::string& profile_id) {
    return profile_config_dir / "profiles" / profile_id / "profile.json";
}

std::string config_fingerprint_for_profile(
    const fs::path& profile_config_dir,
    const btrfsbackup::Profile& profile
) {
    return btrfsbackup::compute_config_fingerprint(
        "2.0.0",
        profile_json_path(profile_config_dir, profile.id),
        {}
    );
}

void write_skipped_status(
    const btrfsbackup::Profile& profile,
    const btrfsbackup::ApplicationPaths& application_paths,
    const btrfsbackup::BackupRequest& request,
    std::size_t source_count
) {
    btrfsbackup::RunStatus status{
        .profile_id = btrfsbackup::ProfileId{profile.id},
        .profile_name = profile.name,
        .run_id = request.run_id,
        .state = btrfsbackup::RunState::Skipped,
        .phase = btrfsbackup::RunPhase::Skipped,
        .message = "A successful backup already exists for today; no new snapshot was created.",
        .current_source_name = "",
        .target_name = profile.target.mapper_name,
        .source_count = static_cast<int>(source_count),
        .started_at = request.timestamp,
        .updated_at = current_local_iso_timestamp(),
        .finished_at = current_local_iso_timestamp(),
        .error = std::nullopt,
        .details = {},
        .can_cancel = false,
        .progress = btrfsbackup::RunProgress{},
        .exit_code = 0,
    };
    btrfsbackup::write_current_status(application_paths.status_root, status);
    btrfsbackup::write_history_entry(application_paths.history_root, status);
}

void write_success_state_for_run(
    const btrfsbackup::Profile& profile,
    const btrfsbackup::ApplicationPaths& application_paths,
    const btrfsbackup::BackupRequest& request,
    const std::string& config_fingerprint,
    std::size_t source_count
) {
    btrfsbackup::write_success_state(
        btrfsbackup::profile_state_dir(application_paths, profile.id),
        btrfsbackup::SuccessState{
            .date = request.today,
            .timestamp = current_local_iso_timestamp(),
            .run_id = request.run_id.value,
            .profile_id = profile.id,
            .profile_name = profile.name,
            .source_count = static_cast<int>(source_count),
            .target_luks_uuid = profile.target.luks_uuid,
            .config_fingerprint = config_fingerprint,
        }
    );
}

fs::path lock_root(const btrfsbackup::BackupServiceDependencies* dependencies) {
    if (dependencies != nullptr && !dependencies->lock_root.empty()) {
        return dependencies->lock_root;
    }
    return btrfsbackup::default_lock_root();
}

btrfsbackup::ApplicationConfig application_config(
    const btrfsbackup::BackupRequest& request,
    btrfsbackup::BackupServiceDependencies* dependencies
) {
    return dependencies == nullptr
        ? btrfsbackup::ApplicationConfig::load(request.profile_config_dir)
        : dependencies->application_config;
}

btrfsbackup::SnapshotMetadataReader metadata_reader(btrfsbackup::BackupServiceDependencies* dependencies) {
    return dependencies != nullptr && dependencies->snapshot_metadata_reader
        ? dependencies->snapshot_metadata_reader
        : btrfsbackup::read_btrfs_snapshot_metadata;
}

} // namespace

namespace btrfsbackup {

BackupRunPlan plan_backup(const BackupRequest& input, BackupServiceDependencies* dependencies) {
    BackupRequest request = normalize_request(input);
    Profile profile = load_profile_by_id(request.profile_config_dir, request.profile_id.value);
    ApplicationConfig config = application_config(request, dependencies);
    return build_plan(
        request,
        profile,
        config.paths(),
        metadata_reader(dependencies),
        dependencies == nullptr
    );
}

BackupExecutionResult start_backup(
    const BackupRequest& input,
    BackupServiceDependencies* dependencies,
    CancellationToken* external_cancellation
) {
    BackupRequest request = normalize_request(input);
    Profile profile = load_profile_by_id(request.profile_config_dir, request.profile_id.value);
    ApplicationConfig config = application_config(request, dependencies);
    const ApplicationPaths& application_paths = config.paths();
    const fs::path state_dir = profile_state_dir(application_paths, profile.id);

    BackupExecutionResult service_result;
    service_result.plan.profile_id = ProfileId{profile.id};
    service_result.plan.run_id = request.run_id;

    std::optional<FileLock> profile_lock;
    std::optional<FileLock> target_lock;
    const fs::path locks = lock_root(dependencies);
    profile_lock.emplace(profile_lock_path(locks, profile.id));
    if (!profile_lock->try_acquire()) {
        service_result.outcome = BackupExecutionOutcome::Busy;
        service_result.error_code = ErrorCode::RunnerProfileBusy;
        service_result.error_message = "Another runner is already active for profile " + profile.id + ".";
        return service_result;
    }
    target_lock.emplace(target_lock_path(locks, profile.target.luks_uuid));
    if (!target_lock->try_acquire()) {
        service_result.outcome = BackupExecutionOutcome::Busy;
        service_result.error_code = ErrorCode::RunnerTargetBusy;
        service_result.error_message =
            "Another operation is already active for target LUKS UUID " + profile.target.luks_uuid + ".";
        return service_result;
    }

    ensure_target_mounted(profile, request);
    service_result.plan = build_plan(
        request,
        profile,
        application_paths,
        metadata_reader(dependencies),
        dependencies == nullptr
    );
    const std::string fingerprint = config_fingerprint_for_profile(request.profile_config_dir, profile);

    if (request.validate_only) {
        service_result.outcome = BackupExecutionOutcome::Validated;
        return service_result;
    }

    if (!request.force
        && profile.settings.daily_limit
        && last_success_matches(state_dir, request.today, profile.target.luks_uuid, fingerprint)) {
        write_skipped_status(profile, application_paths, request, service_result.plan.sources.size());
        service_result.outcome = BackupExecutionOutcome::Skipped;
        return service_result;
    }

    clear_cancel_request(state_dir);
    JsonFileBackupRunCheckpointStore checkpoints(state_dir);
    std::map<std::string, std::string> source_names;
    for (const ProfileSource& source : profile.sources) {
        source_names.emplace(source.id, source.name);
    }
    StatusBackupRunEventSink status_events({
        .status_root = application_paths.status_root,
        .history_root = application_paths.history_root,
        .profile_name = profile.name,
        .source_count = static_cast<int>(service_result.plan.sources.size()),
        .started_at = request.timestamp,
        .source_names = std::move(source_names),
        .target_name = profile.target.mapper_name,
    });

    std::optional<CancellationToken> owned_cancellation;
    if (external_cancellation == nullptr) {
        owned_cancellation.emplace();
        external_cancellation = &*owned_cancellation;
    }
    CancellationToken& cancellation = *external_cancellation;
    CancelRequestMonitor cancellation_monitor(state_dir, cancellation);

    LibBtrfsOperations btrfs;
    PosixFileSystem filesystem_effects;
    PosixCommandRunner command_runner;
    BackupRunActionEffects real_action_effects(
        btrfs,
        filesystem_effects,
        command_runner,
        profile.target.mount_point
    );
    PosixTransferPipeline real_transfer_pipeline;
    IBackupRunActionEffects& action_effects = dependencies == nullptr
        ? static_cast<IBackupRunActionEffects&>(real_action_effects)
        : dependencies->action_effects;
    ITransferPipeline& transfer_pipeline = dependencies == nullptr
        ? static_cast<ITransferPipeline&>(real_transfer_pipeline)
        : dependencies->transfer_pipeline;
    if (dependencies != nullptr) {
        service_result.plan.target_mount_point.clear();
    }
    ThreadedAsyncTransferPipeline async_transfer_pipeline(transfer_pipeline);

    BackupRun run(service_result.plan, action_effects, async_transfer_pipeline, checkpoints);
    BackupRunExecutionResult execution = run.execute(status_events, cancellation);
    clear_cancel_request(state_dir);
    service_result.actions_completed = execution.actions_completed;
    service_result.outcome = execution.completed
        ? BackupExecutionOutcome::Completed
        : (execution.cancelled ? BackupExecutionOutcome::Cancelled : BackupExecutionOutcome::Failed);
    if (execution.completed) {
        write_success_state_for_run(
            profile,
            application_paths,
            request,
            fingerprint,
            service_result.plan.sources.size()
        );
    }
    return service_result;
}

CancelBackupResult cancel_backup(
    const fs::path& profile_config_dir,
    const ProfileId& profile_id,
    BackupServiceDependencies* dependencies
) {
    Profile profile = load_profile_by_id(profile_config_dir, profile_id.value);
    BackupRequest request;
    request.profile_config_dir = profile_config_dir;
    ApplicationConfig config = application_config(request, dependencies);
    write_cancel_request(profile_state_dir(config.paths(), profile.id));
    return {
        .profile_id = ProfileId{profile.id},
        .cancel_requested = true,
    };
}

} // namespace btrfsbackup
