#include <btrfsbackup/command/runner_command.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <btrfsbackup/backup_run_plan.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/mount_info.hpp>
#include <btrfsbackup/pending_recovery_plan.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/run_state.hpp>
#include <btrfsbackup/snapshot_inventory.hpp>

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
        case btrfsbackup::BackupRunActionKind::CreateSnapshot:
            return "create-snapshot";
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
    return {
        {"kind", action_name(action.kind)},
        {"sourceId", action.source_id},
        {"primaryPath", action.primary_path.string()},
        {"secondaryPath", action.secondary_path.string()}
    };
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
              << "  plan --profile ID [--timestamp TS] [--run-id ID] [--mountinfo PATH]\n";
}

} // namespace

namespace btrfsbackup::command {

int runner(const fs::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output) {
    if (args.empty()) {
        usage();
        return 2;
    }

    std::string command = args.at(0);
    if (command == "-h" || command == "--help") {
        usage();
        return 0;
    }
    if (command != "plan") {
        fail("unknown command: " + command);
    }

    std::string profile_id = "default";
    fs::path mountinfo = "/proc/self/mountinfo";
    std::string timestamp = current_utc_timestamp();
    std::string run_id;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args.at(i);
        if (arg == "--profile") {
            profile_id = arg_value(args, i, arg);
        } else if (arg == "--timestamp") {
            timestamp = arg_value(args, i, arg);
        } else if (arg == "--run-id") {
            run_id = arg_value(args, i, arg);
        } else if (arg == "--mountinfo") {
            mountinfo = arg_value(args, i, arg);
        } else {
            fail("unknown plan option: " + arg);
        }
    }

    if (run_id.empty()) {
        run_id = compact_timestamp(timestamp) + "-shadow";
    }

    Profile profile = load_profile_by_id(profile_config_dir, profile_id);
    std::vector<MountEntry> mounts = read_mount_table(mountinfo);

    SnapshotInventoryBySource local_inventory;
    SnapshotInventoryBySource remote_inventory;
    PendingMarkerBySource pending_markers;
    PendingSnapshotBySource pending_snapshots;
    const fs::path profile_state_dir = fs::path(profile.paths.state_dir) / "profiles" / profile.id;

    for (const ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        fs::path remote_dir = fs::path(profile.paths.remote_root) / source.remote_subdir;
        local_inventory[source.id] = list_snapshot_inventory(
            source.local_snapshot_dir,
            source.id,
            SnapshotSide::Local,
            read_btrfs_snapshot_metadata
        );
        remote_inventory[source.id] = list_snapshot_inventory(
            remote_dir,
            source.id,
            SnapshotSide::Remote,
            read_btrfs_snapshot_metadata
        );

        std::optional<PendingMarker> marker = read_pending_marker_if_exists(profile_state_dir, source.id);
        pending_markers[source.id] = marker;
        if (marker.has_value()) {
            pending_snapshots[source.id] = read_btrfs_snapshot_metadata(marker->local_snapshot_path);
        }
    }

    BackupRunPlan plan = build_backup_run_plan(
        profile,
        mounts,
        local_inventory,
        remote_inventory,
        pending_markers,
        pending_snapshots,
        run_id,
        timestamp
    );

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

} // namespace btrfsbackup::command
