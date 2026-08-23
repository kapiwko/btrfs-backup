#include <btrfsbackup/command/runner_command.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <btrfsbackup/backup_run_action_effects.hpp>
#include <btrfsbackup/backup_run_executor.hpp>
#include <btrfsbackup/backup_run_persistence.hpp>
#include <btrfsbackup/backup_run_plan.hpp>
#include <btrfsbackup/btrfs_operations.hpp>
#include <btrfsbackup/config_fingerprint.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/mount_info.hpp>
#include <btrfsbackup/pending_recovery_plan.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_loader.hpp>
#include <btrfsbackup/run_state.hpp>
#include <btrfsbackup/runtime_adapters.hpp>
#include <btrfsbackup/snapshot_inventory.hpp>
#include <btrfsbackup/status_writer.hpp>
#include <btrfsbackup/target_mount_validation.hpp>
#include <btrfsbackup/transfer_pipeline.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl runner: " << message << '\n';
    std::exit(code);
}

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        fail(option + " requires a value");
    }
    return args[++index];
}

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

std::string action_name(btrfsbackup::BackupRunActionKind kind) {
    switch (kind) {
        case btrfsbackup::BackupRunActionKind::RecoverPending:
            return "recover-pending";
        case btrfsbackup::BackupRunActionKind::CleanupIncoming:
            return "cleanup-incoming";
        case btrfsbackup::BackupRunActionKind::BeforeSnapshotHook:
            return "before-snapshot-hook";
        case btrfsbackup::BackupRunActionKind::CreateSnapshot:
            return "create-snapshot";
        case btrfsbackup::BackupRunActionKind::AfterSnapshotHook:
            return "after-snapshot-hook";
        case btrfsbackup::BackupRunActionKind::SelectParent:
            return "select-parent";
        case btrfsbackup::BackupRunActionKind::SendReceive:
            return "send-receive";
        case btrfsbackup::BackupRunActionKind::VerifyReceived:
            return "verify-received";
        case btrfsbackup::BackupRunActionKind::CommitReceived:
            return "commit-received";
        case btrfsbackup::BackupRunActionKind::ApplyRemoteRetention:
            return "apply-remote-retention";
        case btrfsbackup::BackupRunActionKind::ApplyLocalRetention:
            return "apply-local-retention";
        case btrfsbackup::BackupRunActionKind::CleanupSource:
            return "cleanup-source";
    }
    return "unknown";
}

btrfsbackup::Json action_to_json(const btrfsbackup::BackupRunAction& action) {
    btrfsbackup::Json result = {
        {"kind", action_name(action.kind)},
        {"sourceId", action.source_id},
        {"primaryPath", action.primary_path.string()},
        {"secondaryPath", action.secondary_path.string()}
    };
    if (!action.hook.program.empty()) {
        result["hook"] = {
            {"type", "program"},
            {"program", action.hook.program},
            {"arguments", action.hook.arguments}
        };
    }
    return result;
}

btrfsbackup::Json paths_to_json(const std::vector<btrfsbackup::SnapshotInfo>& snapshots) {
    btrfsbackup::Json result = btrfsbackup::Json::array();
    for (const btrfsbackup::SnapshotInfo& snapshot : snapshots) {
        result.push_back(snapshot.path.string());
    }
    return result;
}

void usage() {
    std::cout << "Usage: btrfs-backupctl runner COMMAND\n"
              << "\nCommands:\n"
              << "  plan --profile ID [--timestamp TS] [--run-id ID] [--mountinfo PATH]\n"
              << "  execute --profile ID [--timestamp TS] [--run-id ID] [--force] [--validate]\n";
}

struct RunnerOptions {
    std::string profile_id = "default";
    fs::path mountinfo = "/proc/self/mountinfo";
    std::string timestamp = current_utc_timestamp();
    std::string run_id;
    std::map<std::string, std::string> mount_uuid_overrides;
    std::string today = current_local_date();
    bool force = false;
    bool validate_only = false;
};

RunnerOptions parse_options(const std::string& command, const std::vector<std::string>& args) {
    RunnerOptions options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args.at(i);
        if (arg == "--profile") {
            options.profile_id = arg_value(args, i, arg);
        } else if (arg == "--timestamp") {
            options.timestamp = arg_value(args, i, arg);
        } else if (arg == "--run-id") {
            options.run_id = arg_value(args, i, arg);
        } else if (arg == "--today") {
            options.today = arg_value(args, i, arg);
        } else if (arg == "--mountinfo") {
            options.mountinfo = arg_value(args, i, arg);
        } else if (arg == "--mount-uuid") {
            std::string source = arg_value(args, i, arg);
            std::string uuid = arg_value(args, i, arg);
            options.mount_uuid_overrides[source] = uuid;
        } else if (arg == "--force") {
            options.force = true;
        } else if (arg == "--validate") {
            options.validate_only = true;
        } else {
            fail("unknown " + command + " option: " + arg);
        }
    }

    if (options.run_id.empty()) {
        options.run_id = compact_timestamp(options.timestamp) + "-shadow";
    }
    return options;
}

std::vector<btrfsbackup::MountEntry> read_mounts(const RunnerOptions& options) {
    return options.mount_uuid_overrides.empty()
        ? btrfsbackup::read_mount_table(options.mountinfo)
        : btrfsbackup::read_mount_table(options.mountinfo, [&options](const std::string& source) {
              auto found = options.mount_uuid_overrides.find(source);
              if (found != options.mount_uuid_overrides.end()) {
                  return found->second;
              }
              return btrfsbackup::blkid_filesystem_uuid(source);
          });
}

btrfsbackup::BackupRunPlan build_runner_plan(
    const fs::path& profile_config_dir,
    const RunnerOptions& options,
    btrfsbackup::Profile& profile,
    const btrfsbackup::SnapshotMetadataReader& metadata_reader
) {
    profile = btrfsbackup::load_profile_by_id(profile_config_dir, options.profile_id);
    std::vector<btrfsbackup::MountEntry> mounts = read_mounts(options);
    btrfsbackup::validate_target_mount(profile, mounts);

    btrfsbackup::SnapshotInventoryBySource local_inventory;
    btrfsbackup::SnapshotInventoryBySource remote_inventory;
    btrfsbackup::PendingMarkerBySource pending_markers;
    btrfsbackup::PendingSnapshotBySource pending_snapshots;
    const fs::path profile_state_dir = fs::path(profile.paths.state_dir) / "profiles" / profile.id;

    for (const btrfsbackup::ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        fs::path remote_dir = fs::path(profile.paths.remote_root) / source.remote_subdir;
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

        std::optional<btrfsbackup::PendingMarker> marker = btrfsbackup::read_pending_marker_if_exists(profile_state_dir, source.id);
        pending_markers[source.id] = marker;
        if (marker.has_value()) {
            pending_snapshots[source.id] = metadata_reader(marker->local_snapshot_path);
        }
    }

    return btrfsbackup::build_backup_run_plan(
        profile,
        mounts,
        local_inventory,
        remote_inventory,
        pending_markers,
        pending_snapshots,
        options.run_id,
        options.timestamp
    );
}

fs::path profile_json_path(const fs::path& profile_config_dir, const std::string& profile_id) {
    return profile_config_dir / "profiles" / profile_id / "profile.json";
}

std::string config_fingerprint_for_profile(const fs::path& profile_config_dir, const btrfsbackup::Profile& profile) {
    return btrfsbackup::compute_config_fingerprint("2.0.0", profile_json_path(profile_config_dir, profile.id), {});
}

void write_skipped_status(const btrfsbackup::Profile& profile, const RunnerOptions& options, std::size_t source_count) {
    btrfsbackup::StatusRecord record{
        .profile_id = profile.id,
        .profile_name = profile.name,
        .run_id = options.run_id,
        .state = "skipped",
        .phase = "skipped",
        .message = "A successful backup already exists for today; no new snapshot was created.",
        .current_source_name = "",
        .source_count = static_cast<int>(source_count),
        .started_at = options.timestamp,
        .updated_at = current_local_iso_timestamp(),
        .finished_at = current_local_iso_timestamp(),
        .error_code = "",
        .error_message = "",
        .suggested_action = "",
        .exit_code = 0,
    };
    btrfsbackup::write_current_status(profile.paths.status_root, record);
    btrfsbackup::write_history_entry(profile.paths.history_root, record);
}

void write_success_state_for_run(
    const btrfsbackup::Profile& profile,
    const RunnerOptions& options,
    const std::string& config_fingerprint,
    std::size_t source_count
) {
    btrfsbackup::write_success_state(
        fs::path(profile.paths.state_dir) / "profiles" / profile.id,
        btrfsbackup::SuccessState{
            .date = options.today,
            .timestamp = current_local_iso_timestamp(),
            .run_id = options.run_id,
            .profile_id = profile.id,
            .profile_name = profile.name,
            .source_count = static_cast<int>(source_count),
            .target_luks_uuid = profile.target.luks_uuid,
            .config_fingerprint = config_fingerprint,
        }
    );
}

} // namespace

namespace btrfsbackup::command {

int runner(
    const fs::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    RunnerExecutionServices* execution_services
) {
    if (args.empty()) {
        usage();
        return 2;
    }

    std::string command = args.at(0);
    if (command == "-h" || command == "--help") {
        usage();
        return 0;
    }
    if (command != "plan" && command != "execute") {
        fail("unknown command: " + command);
    }

    RunnerOptions options = parse_options(command, args);
    Profile profile;
    SnapshotMetadataReader metadata_reader = execution_services != nullptr && execution_services->snapshot_metadata_reader
        ? execution_services->snapshot_metadata_reader
        : read_btrfs_snapshot_metadata;
    BackupRunPlan plan = build_runner_plan(profile_config_dir, options, profile, metadata_reader);
    const std::string config_fingerprint = config_fingerprint_for_profile(profile_config_dir, profile);

    if (command == "execute") {
        if (options.validate_only) {
            output << Json{
                {"schemaVersion", 1},
                {"mode", "cpp-validate"},
                {"profileId", plan.profile_id},
                {"runId", plan.run_id},
                {"completed", true},
                {"cancelled", false},
                {"actionsCompleted", 0}
            }.dump(2) << '\n';
            return 0;
        }

        if (!options.force
            && profile.settings.daily_limit
            && btrfsbackup::last_success_matches(
                fs::path(profile.paths.state_dir) / "profiles" / profile.id,
                options.today,
                profile.target.luks_uuid,
                config_fingerprint
            )) {
            write_skipped_status(profile, options, plan.sources.size());
            output << Json{
                {"schemaVersion", 1},
                {"mode", "cpp-execute"},
                {"profileId", plan.profile_id},
                {"runId", plan.run_id},
                {"completed", true},
                {"skipped", true},
                {"cancelled", false},
                {"actionsCompleted", 0}
            }.dump(2) << '\n';
            return 0;
        }

        JsonFileBackupRunCheckpointStore checkpoints(fs::path(profile.paths.state_dir) / "profiles" / profile.id);
        StatusBackupRunEventSink status_events({
            .status_root = profile.paths.status_root,
            .history_root = profile.paths.history_root,
            .profile_name = profile.name,
            .source_count = static_cast<int>(plan.sources.size()),
            .started_at = options.timestamp,
        });
        CancellationToken cancellation;

        LibBtrfsOperations btrfs;
        StdFileSystemEffects fs_effects;
        SystemCommandRunner command_runner;
        BackupRunActionEffects real_action_effects(btrfs, fs_effects, command_runner);
        PosixTransferPipeline real_transfer_pipeline;
        IBackupRunActionEffects& action_effects = execution_services == nullptr
            ? static_cast<IBackupRunActionEffects&>(real_action_effects)
            : execution_services->action_effects;
        ITransferPipeline& transfer_pipeline = execution_services == nullptr
            ? static_cast<ITransferPipeline&>(real_transfer_pipeline)
            : execution_services->transfer_pipeline;

        BackupRunExecutor executor(action_effects, transfer_pipeline, checkpoints);
        BackupRunExecutionResult result = executor.execute(plan, status_events, cancellation);
        if (result.completed) {
            write_success_state_for_run(profile, options, config_fingerprint, plan.sources.size());
        }

        output << Json{
            {"schemaVersion", 1},
            {"mode", "cpp-execute"},
            {"profileId", plan.profile_id},
            {"runId", plan.run_id},
            {"completed", result.completed},
            {"skipped", false},
            {"cancelled", result.cancelled},
            {"actionsCompleted", result.actions_completed}
        }.dump(2) << '\n';
        return result.completed ? 0 : 1;
    }

    Json sources = Json::array();
    for (const BackupSourceRunPlan& source : plan.sources) {
        Json actions = Json::array();
        for (const BackupRunAction& action : source.actions) {
            actions.push_back(action_to_json(action));
        }
        sources.push_back({
            {"sourceId", source.source_id},
            {"sourceSubvolume", source.source_subvolume.string()},
            {"localSnapshotPath", source.local_snapshot_path.string()},
            {"remoteSnapshotDir", source.remote_snapshot_dir.string()},
            {"incomingRunDir", source.incoming_run_dir.string()},
            {"receivedSnapshotPath", source.received_snapshot_path.string()},
            {"finalRemoteSnapshotPath", source.final_remote_snapshot_path.string()},
            {"incremental", source.parent.incremental},
            {"parentPath", source.parent.local_parent.has_value() ? Json(source.parent.local_parent->path.string()) : Json(nullptr)},
            {"pendingRecoveryAction", action_name(source.actions.front().kind) == "recover-pending" ? "recover-pending" : "none"},
            {"localRetentionDelete", paths_to_json(source.local_retention.delete_snapshots)},
            {"remoteRetentionDelete", paths_to_json(source.remote_retention.delete_snapshots)},
            {"actions", actions}
        });
    }

    output << Json{
        {"schemaVersion", 1},
        {"mode", "shadow-plan"},
        {"profileId", plan.profile_id},
        {"runId", plan.run_id},
        {"sources", sources}
    }.dump(2) << '\n';

    return 0;
}

int runner(const fs::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output) {
    return runner(profile_config_dir, args, output, nullptr);
}

} // namespace btrfsbackup::command
