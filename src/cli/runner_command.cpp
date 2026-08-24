#include <cli/runner_command.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <config/json.hpp>

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
        {"sourceId", action.source_id.value},
        {"primaryPath", action.primary_path.string()},
        {"secondaryPath", action.secondary_path.string()}
    };
    if (!action.hook.program.empty()) {
        result["hook"] = {
            {"type", "program"},
            {"program", action.hook.program},
            {"arguments", action.hook.arguments},
            {"timeoutSeconds", action.hook.timeout_seconds}
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

btrfsbackup::Json source_plan_to_json(const btrfsbackup::BackupSourceRunPlan& source, bool include_actions) {
    btrfsbackup::Json result = {
        {"sourceId", source.source_id.value},
        {"sourceSubvolume", source.source_subvolume.string()},
        {"localSnapshotPath", source.local_snapshot_path.string()},
        {"remoteSnapshotDir", source.remote_snapshot_dir.string()},
        {"incomingRunDir", source.incoming_run_dir.string()},
        {"receivedSnapshotPath", source.received_snapshot_path.string()},
        {"finalRemoteSnapshotPath", source.final_remote_snapshot_path.string()},
        {"incremental", source.parent.incremental},
        {"parentPath", source.parent.local_parent.has_value()
            ? btrfsbackup::Json(source.parent.local_parent->path.string())
            : btrfsbackup::Json(nullptr)},
        {"pendingRecoveryAction", action_name(source.actions.front().kind) == "recover-pending"
            ? "recover-pending"
            : "none"},
        {"localRetentionDelete", paths_to_json(source.local_retention.delete_snapshots)},
        {"remoteRetentionDelete", paths_to_json(source.remote_retention.delete_snapshots)}
    };
    if (include_actions) {
        btrfsbackup::Json actions = btrfsbackup::Json::array();
        for (const btrfsbackup::BackupRunAction& action : source.actions) {
            actions.push_back(action_to_json(action));
        }
        result["actions"] = actions;
    }
    return result;
}

btrfsbackup::Json sources_to_json(
    const std::vector<btrfsbackup::BackupSourceRunPlan>& sources,
    bool include_actions
) {
    btrfsbackup::Json result = btrfsbackup::Json::array();
    for (const btrfsbackup::BackupSourceRunPlan& source : sources) {
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

btrfsbackup::BackupRequest parse_request(
    const fs::path& profile_config_dir,
    const std::string& command,
    const std::vector<std::string>& args
) {
    btrfsbackup::BackupRequest request;
    request.profile_config_dir = profile_config_dir;
    request.timestamp = current_utc_timestamp();
    request.today = current_local_date();
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args.at(i);
        if (arg == "--profile") {
            request.profile_id = btrfsbackup::ProfileId{arg_value(args, i, arg)};
        } else if (arg == "--timestamp") {
            request.timestamp = arg_value(args, i, arg);
        } else if (arg == "--run-id") {
            request.run_id = btrfsbackup::RunId{arg_value(args, i, arg)};
        } else if (arg == "--today") {
            request.today = arg_value(args, i, arg);
        } else if (arg == "--mountinfo") {
            request.mountinfo = arg_value(args, i, arg);
        } else if (arg == "--mount-uuid") {
            std::string source = arg_value(args, i, arg);
            request.mount_uuid_overrides[source] = arg_value(args, i, arg);
        } else if (arg == "--force") {
            request.force = true;
        } else if (arg == "--validate") {
            request.validate_only = true;
        } else {
            fail("unknown " + command + " option: " + arg);
        }
    }
    if (request.run_id.value.empty()) {
        request.run_id = btrfsbackup::RunId{compact_timestamp(request.timestamp) + "-shadow"};
    }
    return request;
}

int print_execution_result(const btrfsbackup::BackupExecutionResult& result, std::ostream& output) {
    using btrfsbackup::BackupExecutionOutcome;
    if (result.outcome == BackupExecutionOutcome::Busy) {
        output << btrfsbackup::Json{
            {"schemaVersion", 1},
            {"mode", "cpp-execute"},
            {"profileId", result.plan.profile_id.value},
            {"runId", result.plan.run_id.value},
            {"completed", false},
            {"skipped", false},
            {"cancelled", false},
            {"busy", true},
            {"actionsCompleted", 0},
            {"errorCode", result.error_code.has_value() ? error_code_name(*result.error_code) : ""},
            {"errorMessage", result.error_message}
        }.dump(2) << '\n';
        return 1;
    }
    if (result.outcome == BackupExecutionOutcome::Validated) {
        output << btrfsbackup::Json{
            {"schemaVersion", 1},
            {"mode", "cpp-validate"},
            {"profileId", result.plan.profile_id.value},
            {"runId", result.plan.run_id.value},
            {"completed", true},
            {"cancelled", false},
            {"actionsCompleted", 0}
        }.dump(2) << '\n';
        return 0;
    }

    const bool completed = result.outcome == BackupExecutionOutcome::Completed
        || result.outcome == BackupExecutionOutcome::Skipped;
    const bool skipped = result.outcome == BackupExecutionOutcome::Skipped;
    const bool cancelled = result.outcome == BackupExecutionOutcome::Cancelled;
    output << btrfsbackup::Json{
        {"schemaVersion", 1},
        {"mode", "cpp-execute"},
        {"profileId", result.plan.profile_id.value},
        {"runId", result.plan.run_id.value},
        {"completed", completed},
        {"skipped", skipped},
        {"cancelled", cancelled},
        {"actionsCompleted", result.actions_completed},
        {"sources", sources_to_json(result.plan.sources, false)}
    }.dump(2) << '\n';
    return completed ? 0 : 1;
}

} // namespace

namespace btrfsbackup::command {

int runner(
    const fs::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    RunnerExecutionServices* execution_services,
    CancellationToken* external_cancellation
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

    BackupRequest request = parse_request(profile_config_dir, command, args);
    if (command == "cancel") {
        CancelBackupResult result = cancel_backup(
            profile_config_dir,
            request.profile_id,
            execution_services
        );
        output << Json{
            {"schemaVersion", 1},
            {"mode", "cpp-cancel"},
            {"profileId", result.profile_id.value},
            {"cancelRequested", result.cancel_requested}
        }.dump(2) << '\n';
        return 0;
    }
    if (command == "plan") {
        BackupRunPlan plan = plan_backup(request, execution_services);
        output << Json{
            {"schemaVersion", 1},
            {"mode", "shadow-plan"},
            {"profileId", plan.profile_id.value},
            {"runId", plan.run_id.value},
            {"sources", sources_to_json(plan.sources, true)}
        }.dump(2) << '\n';
        return 0;
    }
    return print_execution_result(
        start_backup(request, execution_services, external_cancellation),
        output
    );
}

int runner(const fs::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output) {
    return runner(profile_config_dir, args, output, nullptr);
}

} // namespace btrfsbackup::command
