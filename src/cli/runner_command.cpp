// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/runner_command.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/backup_run.hpp>
#include <backup/backup_planner.hpp>
#include <state/file_run_state_repository.hpp>
#include <state/file_pending_marker_store.hpp>
#include <backup/system_run_context.hpp>
#include <backup/action_handlers/hook_action_handler.hpp>
#include <backup/action_handlers/recovery_action_handler.hpp>
#include <backup/action_handlers/repository_action_handler.hpp>
#include <backup/action_handlers/retention_action_handler.hpp>
#include <backup/action_handlers/snapshot_action_handler.hpp>
#include <backup/transfer/async_transfer.hpp>
#include <backup/action_handlers/transfer_action_handler.hpp>
#include <platform/linux/btrfs_util_operations.hpp>
#include <platform/linux/config/application_config.hpp>
#include <platform/linux/posix_command_runner.hpp>
#include <platform/linux/file_lock.hpp>
#include <platform/linux/file_backup_run_lease_provider.hpp>
#include <platform/linux/file_io.hpp>
#include <platform/linux/posix_filesystem.hpp>
#include <platform/linux/mount_info.hpp>
#include <platform/linux/posix_transfer_pipeline.hpp>
#include <platform/linux/safe_directory_root.hpp>
#include <platform/linux/systemd_target_manager.hpp>
#include <platform/linux/trusted_executable.hpp>
#include <config/model/json.hpp>
#include <config/model/profile_document.hpp>
#include <platform/linux/config/profile_repository.hpp>
#include <platform/linux/config/profile_runtime_policy.hpp>

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

std::string action_name(btrfsbackup::backup::BackupRunActionKind kind) {
    switch (kind) {
    case btrfsbackup::backup::BackupRunActionKind::RecoverPending:
        return "recover-pending";
    case btrfsbackup::backup::BackupRunActionKind::CleanupIncoming:
        return "cleanup-incoming";
    case btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook:
        return "before-snapshot-hook";
    case btrfsbackup::backup::BackupRunActionKind::CreateSnapshot:
        return "create-snapshot";
    case btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook:
        return "after-snapshot-hook";
    case btrfsbackup::backup::BackupRunActionKind::SelectParent:
        return "select-parent";
    case btrfsbackup::backup::BackupRunActionKind::SendReceive:
        return "send-receive";
    case btrfsbackup::backup::BackupRunActionKind::VerifyReceived:
        return "verify-received";
    case btrfsbackup::backup::BackupRunActionKind::CommitReceived:
        return "commit-received";
    case btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention:
        return "apply-remote-retention";
    case btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention:
        return "apply-local-retention";
    case btrfsbackup::backup::BackupRunActionKind::CleanupSource:
        return "cleanup-source";
    }
    return "unknown";
}

btrfsbackup::config::Json action_to_json(
    const btrfsbackup::backup::BackupRunAction& action,
    const btrfsbackup::backup::BackupSourceRunPlan& source
) {
    const auto [primary_path, secondary_path] = std::visit([&](const auto& typed_action) {
        using Action = std::decay_t<decltype(typed_action)>;
        if constexpr (std::is_same_v<Action, btrfsbackup::backup::RecoverPendingAction>) {
            return std::pair{typed_action.recovery.local_snapshot_path, fs::path{}};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::CleanupIncomingAction>) {
            return std::pair{typed_action.incoming_directory, fs::path{}};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::CreateSnapshotAction>) {
            return std::pair{typed_action.snapshot, typed_action.source};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::SelectParentAction>) {
            return std::pair{typed_action.parent.value_or(fs::path{}), fs::path{}};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::SendReceiveAction>) {
            return std::pair{typed_action.snapshot, typed_action.incoming_run_directory};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::VerifyReceivedAction>) {
            return std::pair{typed_action.received_snapshot, typed_action.local_snapshot};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::CommitReceivedAction>) {
            return std::pair{typed_action.received_snapshot, typed_action.final_snapshot};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::ApplyRemoteRetentionAction>) {
            return std::pair{source.remote_snapshot_dir, fs::path{}};
        } else if constexpr (std::is_same_v<Action, btrfsbackup::backup::ApplyLocalRetentionAction>) {
            return std::pair{source.local_snapshot_dir, fs::path{}};
        } else {
            return std::pair{fs::path{}, fs::path{}};
        }
    },
                                                           action);
    btrfsbackup::config::Json result = {
        {"kind", action_name(btrfsbackup::backup::backup_run_action_kind(action))},
        {"sourceId", std::string(btrfsbackup::backup::backup_run_action_source_id(action).value())},
        {"primaryPath", primary_path.string()},
        {"secondaryPath", secondary_path.string()}
    };
    if (const auto* hook_action = std::get_if<btrfsbackup::backup::RunHookAction>(&action)) {
        result["hook"] = {
            {"type", "program"},
            {"program", hook_action->hook.program},
            {"arguments", hook_action->hook.arguments},
            {"timeoutSeconds", hook_action->hook.timeout.count()}
        };
    }
    return result;
}

btrfsbackup::config::Json paths_to_json(const std::vector<btrfsbackup::backup::SnapshotInfo>& snapshots) {
    btrfsbackup::config::Json result = btrfsbackup::config::Json::array();
    for (const btrfsbackup::backup::SnapshotInfo& snapshot : snapshots) {
        result.push_back(snapshot.path.string());
    }
    return result;
}

btrfsbackup::config::Json source_plan_to_json(const btrfsbackup::backup::BackupSourceRunPlan& source, bool include_actions) {
    btrfsbackup::config::Json result = {
        {"sourceId", std::string(source.source_id.value())},
        {"sourceSubvolume", source.source_subvolume.string()},
        {"localSnapshotPath", source.local_snapshot_path.string()},
        {"remoteSnapshotDir", source.remote_snapshot_dir.string()},
        {"incomingRunDir", source.incoming_run_dir.string()},
        {"receivedSnapshotPath", source.received_snapshot_path.string()},
        {"finalRemoteSnapshotPath", source.final_remote_snapshot_path.string()},
        {"incremental", source.parent.incremental},
        {"parentPath", source.parent.local_parent.has_value() ? btrfsbackup::config::Json(source.parent.local_parent->path.string()) : btrfsbackup::config::Json(nullptr)},
        {"pendingRecoveryAction", std::holds_alternative<btrfsbackup::backup::RecoverPendingAction>(source.actions.front()) ? "recover-pending" : "none"},
        {"localRetentionDelete", paths_to_json(source.local_retention.delete_snapshots)},
        {"remoteRetentionDelete", paths_to_json(source.remote_retention.delete_snapshots)}
    };
    if (include_actions) {
        btrfsbackup::config::Json actions = btrfsbackup::config::Json::array();
        for (const btrfsbackup::backup::BackupRunAction& action : source.actions) {
            actions.push_back(action_to_json(action, source));
        }
        result["actions"] = actions;
    }
    return result;
}

btrfsbackup::config::Json sources_to_json(
    const std::vector<btrfsbackup::backup::BackupSourceRunPlan>& sources,
    bool include_actions
) {
    btrfsbackup::config::Json result = btrfsbackup::config::Json::array();
    for (const btrfsbackup::backup::BackupSourceRunPlan& source : sources) {
        result.push_back(source_plan_to_json(source, include_actions));
    }
    return result;
}

void usage() {
    std::cout << "Usage: btrfs-backupctl runner COMMAND\n"
              << "\nCommands:\n"
              << "  plan --profile ID [--timestamp TS] [--run-id ID] [--mountinfo PATH]\n"
              << "  execute --profile ID [--timestamp TS] [--run-id ID] [--force] [--validate]\n"
              << "  cancel --profile ID\n";
}

struct ParsedRunnerCommand {
    btrfsbackup::backup::BackupRequest request{.profile_id = btrfsbackup::ProfileId{"default"}};
    fs::path mountinfo = "/proc/self/mountinfo";
    std::map<std::string, std::string> mount_uuid_overrides;
    std::string timestamp = current_utc_timestamp();
    std::string today = current_local_date();
    std::optional<btrfsbackup::RunId> run_id;
};

ParsedRunnerCommand parse_request(
    const fs::path& profile_config_dir,
    const std::string& command,
    const std::vector<std::string>& args
) {
    (void)profile_config_dir;
    ParsedRunnerCommand parsed;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args.at(i);
        if (arg == "--profile") {
            parsed.request.profile_id = btrfsbackup::ProfileId{arg_value(args, i, arg)};
        } else if (arg == "--timestamp") {
            parsed.timestamp = arg_value(args, i, arg);
        } else if (arg == "--run-id") {
            parsed.run_id = btrfsbackup::RunId{arg_value(args, i, arg)};
        } else if (arg == "--today") {
            parsed.today = arg_value(args, i, arg);
        } else if (arg == "--mountinfo") {
            parsed.mountinfo = arg_value(args, i, arg);
        } else if (arg == "--mount-uuid") {
            std::string source = arg_value(args, i, arg);
            parsed.mount_uuid_overrides[source] = arg_value(args, i, arg);
        } else if (arg == "--force") {
            parsed.request.force = true;
        } else if (arg == "--validate") {
            parsed.request.validate_only = true;
        } else {
            fail("unknown " + command + " option: " + arg);
        }
    }
    if (!parsed.run_id.has_value()) {
        parsed.run_id = btrfsbackup::RunId{compact_timestamp(parsed.timestamp) + "-shadow"};
    }
    return parsed;
}

class CommandClock final : public btrfsbackup::backup::IClock {
  public:
    CommandClock(std::string timestamp, std::string today)
        : timestamp_(std::move(timestamp)), today_(std::move(today)) {
    }

    std::string snapshot_timestamp() const override {
        return timestamp_;
    }
    std::string local_date() const override {
        return today_;
    }
    std::string local_timestamp() const override {
        return current_local_iso_timestamp();
    }

  private:
    std::string timestamp_;
    std::string today_;
};

class CommandRunIdGenerator final : public btrfsbackup::backup::IRunIdGenerator {
  public:
    explicit CommandRunIdGenerator(btrfsbackup::RunId run_id) : run_id_(std::move(run_id)) {
    }

    btrfsbackup::RunId generate(const std::string&) override {
        return run_id_;
    }

  private:
    btrfsbackup::RunId run_id_;
};

class PosixBackupRunFactory final : public btrfsbackup::backup::IBackupRunFactory {
  public:
    PosixBackupRunFactory(
        btrfsbackup::backup::IBtrfsOperations& btrfs,
        btrfsbackup::backup::IFileSystem& filesystem,
        btrfsbackup::backup::ICommandRunner& commands,
        btrfsbackup::backup::transfer::ITransferPipeline& transfers,
        btrfsbackup::IDurableFileOperations& durable_files,
        btrfsbackup::backup::IPendingMarkerStore& pending_markers,
        const btrfsbackup::backup::ISafeDirectoryRootFactory& safe_directories
    )
        : btrfs_(btrfs),
          filesystem_(filesystem),
          commands_(commands),
          transfers_(transfers),
          durable_files_(durable_files),
          pending_markers_(pending_markers),
          safe_directories_(safe_directories) {
    }

    btrfsbackup::backup::BackupRunExecutionResult execute(
        btrfsbackup::backup::BackupRunPlan plan,
        btrfsbackup::backup::IBackupRunEventSink& events,
        btrfsbackup::backup::IBackupRunCheckpointStore& checkpoints,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        btrfsbackup::backup::SnapshotActionHandler snapshots(
            btrfs_,
            filesystem_,
            pending_markers_,
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/")
        );
        btrfsbackup::backup::RecoveryActionHandler recovery(
            btrfs_,
            pending_markers_,
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/"),
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(plan.target_mount_point)
        );
        btrfsbackup::backup::RetentionActionHandler retention(
            btrfs_,
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/"),
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(plan.target_mount_point)
        );
        btrfsbackup::platform::linux::PosixTrustedExecutableResolver hook_executables(
            btrfsbackup::platform::linux::trusted_hook_directory
        );
        btrfsbackup::backup::HookActionHandler hooks(commands_, hook_executables);
        btrfsbackup::backup::RepositoryActionHandler repository(
            btrfs_,
            filesystem_,
            pending_markers_,
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/"),
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(plan.target_mount_point)
        );
        btrfsbackup::backup::TransferActionHandler transfer(
            filesystem_,
            std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(plan.target_mount_point)
        );
        btrfsbackup::backup::BackupRunActionHandler action_handler(
            snapshots,
            recovery,
            retention,
            hooks,
            repository,
            transfer
        );
        btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers_);
        btrfsbackup::backup::BackupRun run(
            std::move(plan),
            action_handler,
            async_transfers,
            checkpoints,
            safe_directories_
        );
        return run.execute(events, cancellation);
    }

  private:
    btrfsbackup::backup::IBtrfsOperations& btrfs_;
    btrfsbackup::backup::IFileSystem& filesystem_;
    btrfsbackup::backup::ICommandRunner& commands_;
    btrfsbackup::backup::transfer::ITransferPipeline& transfers_;
    btrfsbackup::IDurableFileOperations& durable_files_;
    btrfsbackup::backup::IPendingMarkerStore& pending_markers_;
    const btrfsbackup::backup::ISafeDirectoryRootFactory& safe_directories_;
};

class ProductionBackupComposition {
  public:
    ProductionBackupComposition(
        const fs::path& config_root,
        const ParsedRunnerCommand& parsed,
        btrfsbackup::CancellationToken& cancellation
    )
        : config_(btrfsbackup::platform::linux::load_application_config(config_root)),
          profiles_(config_root, config_),
          mounts_(parsed.mountinfo, [&parsed](const std::string& source) {
              const auto found = parsed.mount_uuid_overrides.find(source);
              return found == parsed.mount_uuid_overrides.end()
                  ? btrfsbackup::platform::linux::blkid_filesystem_uuid(source)
                  : found->second;
          }),
          target_mounter_(mounts_, commands_), pending_markers_(durable_files_), planner_(btrfsbackup::platform::linux::read_btrfs_snapshot_metadata, pending_markers_, safe_directories_), run_factory_(btrfs_, filesystem_, commands_, transfers_, durable_files_, pending_markers_, safe_directories_), leases_(btrfsbackup::platform::linux::default_lock_root()), state_(config_.paths(), durable_files_), cancellation_monitor_(state_), clock_(parsed.timestamp, parsed.today), run_ids_(*parsed.run_id), service_(profiles_, config_.paths(), mounts_, target_mounter_, planner_, run_factory_, leases_, state_, cancellation_monitor_, clock_, run_ids_, cancellation) {
    }

    btrfsbackup::backup::BackupService& service() {
        return service_;
    }

  private:
    btrfsbackup::config::ApplicationConfig config_;
    btrfsbackup::platform::linux::FileProfileRepository profiles_;
    btrfsbackup::platform::linux::LinuxMountInspector mounts_;
    btrfsbackup::platform::linux::PosixCommandRunner commands_;
    btrfsbackup::platform::linux::SystemdTargetManager target_mounter_;
    btrfsbackup::platform::linux::SafeDirectoryRootFactory safe_directories_;
    btrfsbackup::platform::linux::LibBtrfsOperations btrfs_;
    btrfsbackup::platform::linux::PosixFileSystem filesystem_;
    btrfsbackup::platform::linux::PosixTransferPipeline transfers_;
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files_;
    btrfsbackup::state::FilePendingMarkerStore pending_markers_;
    btrfsbackup::backup::BackupPlanner planner_;
    PosixBackupRunFactory run_factory_;
    btrfsbackup::platform::linux::FileBackupRunLeaseProvider leases_;
    btrfsbackup::state::FileRunStateRepository state_;
    btrfsbackup::state::FileCancellationMonitor cancellation_monitor_;
    CommandClock clock_;
    CommandRunIdGenerator run_ids_;
    btrfsbackup::backup::BackupService service_;
};

int print_execution_result(const btrfsbackup::backup::BackupExecutionResult& result, std::ostream& output) {
    using btrfsbackup::backup::BackupExecutionOutcome;
    if (result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Busy) {
        output << btrfsbackup::config::Json{
                      {"schemaVersion", 1},
                      {"mode", "cpp-execute"},
                      {"profileId", std::string(result.plan.profile_id.value())},
                      {"runId", std::string(result.plan.run_id.value())},
                      {"completed", false},
                      {"skipped", false},
                      {"cancelled", false},
                      {"busy", true},
                      {"actionsCompleted", 0},
                      {"errorCode", result.error_code.has_value() ? error_code_name(*result.error_code) : ""},
                      {"errorMessage", result.error_message}
                  }.dump(2)
               << '\n';
        return 1;
    }
    if (result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Validated) {
        output << btrfsbackup::config::Json{
                      {"schemaVersion", 1},
                      {"mode", "cpp-validate"},
                      {"profileId", std::string(result.plan.profile_id.value())},
                      {"runId", std::string(result.plan.run_id.value())},
                      {"completed", true},
                      {"cancelled", false},
                      {"actionsCompleted", 0}
                  }
                      .dump(2)
               << '\n';
        return 0;
    }

    const bool completed = result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Completed || result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Skipped;
    const bool skipped = result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Skipped;
    const bool cancelled = result.outcome == btrfsbackup::backup::BackupExecutionOutcome::Cancelled;
    output << btrfsbackup::config::Json{
                  {"schemaVersion", 1},
                  {"mode", "cpp-execute"},
                  {"profileId", std::string(result.plan.profile_id.value())},
                  {"runId", std::string(result.plan.run_id.value())},
                  {"completed", completed},
                  {"skipped", skipped},
                  {"cancelled", cancelled},
                  {"actionsCompleted", result.actions_completed},
                  {"sources", sources_to_json(result.plan.sources, false)}
              }.dump(2)
           << '\n';
    return completed ? 0 : 1;
}

} // namespace

namespace btrfsbackup::cli {

int runner(
    const std::vector<std::string>& args,
    std::ostream& output,
    btrfsbackup::backup::BackupService& service
) {
    if (args.empty()) {
        usage();
        return 2;
    }
    const std::string& command = args.at(0);
    if (command == "-h" || command == "--help") {
        usage();
        return 0;
    }
    if (command != "plan" && command != "execute" && command != "cancel") {
        fail("unknown command: " + command);
    }

    const btrfsbackup::backup::BackupRequest request = parse_request({}, command, args).request;
    if (command == "cancel") {
        btrfsbackup::backup::CancelBackupResult result = service.cancel(request.profile_id);
        output << btrfsbackup::config::Json{
                      {"schemaVersion", 1},
                      {"mode", "cpp-cancel"},
                      {"profileId", std::string(result.profile_id.value())},
                      {"cancelRequested", result.cancel_requested}
                  }.dump(2)
               << '\n';
        return 0;
    }
    if (command == "plan") {
        btrfsbackup::backup::BackupRunPlan plan = service.plan(request);
        output << btrfsbackup::config::Json{
                      {"schemaVersion", 1},
                      {"mode", "shadow-plan"},
                      {"profileId", std::string(plan.profile_id.value())},
                      {"runId", std::string(plan.run_id.value())},
                      {"sources", sources_to_json(plan.sources, true)}
                  }.dump(2)
               << '\n';
        return 0;
    }
    return print_execution_result(service.start(request), output);
}

int runner(const fs::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output) {
    CancellationToken cancellation;
    return runner(profile_config_dir, args, output, cancellation);
}

int runner(
    const fs::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    CancellationToken& cancellation
) {
    if (args.empty()) {
        usage();
        return 2;
    }
    if (args.front() == "-h" || args.front() == "--help") {
        usage();
        return 0;
    }
    if (args.front() != "plan" && args.front() != "execute" && args.front() != "cancel") {
        fail("unknown command: " + args.front());
    }
    const ParsedRunnerCommand parsed = parse_request(profile_config_dir, args.front(), args);
    ProductionBackupComposition composition(profile_config_dir, parsed, cancellation);
    return runner(args, output, composition.service());
}

} // namespace btrfsbackup::cli
