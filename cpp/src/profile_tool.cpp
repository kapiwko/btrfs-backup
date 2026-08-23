#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <btrfsbackup/profile_tool.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_render.hpp>
#include <btrfsbackup/profile_store.hpp>

namespace fs = std::filesystem;
using btrfsbackup::ValidationError;
using btrfsbackup::atomic_write;
using btrfsbackup::dump_json;
using btrfsbackup::Json;
using btrfsbackup::load_json_file;
using btrfsbackup::load_profile_by_id;
using btrfsbackup::Profile;
using btrfsbackup::profile_from_json;
using btrfsbackup::profile_to_json;
using btrfsbackup::render_profile_env;
using btrfsbackup::render_tree;
using btrfsbackup::save_tree;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl profile: " << message << '\n';
    std::exit(code);
}

std::string arg_value(std::size_t& index, const std::vector<std::string>& args, const std::string& option) {
    if (index + 1 >= args.size()) {
        fail(option + " requires a value");
    }
    return args[++index];
}

long long arg_int(const std::string& value, const std::string& option) {
    try {
        std::size_t pos = 0;
        long long result = std::stoll(value, &pos);
        if (pos != value.size() || result < 0) {
            fail(option + " must be a non-negative integer");
        }
        return result;
    } catch (const std::exception&) {
        fail(option + " must be a non-negative integer");
    }
}

bool arg_bool(const std::string& value, const std::string& option) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    fail(option + " must be true or false");
}

int command_create_profile(const std::vector<std::string>& args) {
    fs::path output;
    std::string profile_id = "default";
    std::string profile_name = "Default backup";
    std::string device;
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string partition_uuid;
    std::string serial;
    std::string mapper_name;
    std::string mount_point;
    std::string remote_root;
    std::string incoming_root;
    std::string state_dir = "/var/lib/btrfs-backup";
    std::string status_root = "/run/btrfs-backup/profiles";
    std::string history_root = "/var/lib/btrfs-backup/history";
    bool daily_limit = true;
    bool incremental_required = true;
    bool keep_failed_local_snapshot = false;
    bool auto_eject = true;
    long long remote_retention = 30;
    long long local_retention = 30;
    long long minimum_target_free_bytes = 5368709120LL;
    long long minimum_local_free_bytes = 1073741824LL;
    bool notify_enabled = true;
    std::string notify_user;
    std::string notify_method = "auto";
    Json sources = Json::array();

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--output") {
            output = arg_value(i, args, arg);
        } else if (arg == "--profile") {
            profile_id = arg_value(i, args, arg);
        } else if (arg == "--name") {
            profile_name = arg_value(i, args, arg);
        } else if (arg == "--device") {
            device = arg_value(i, args, arg);
        } else if (arg == "--luks-uuid") {
            luks_uuid = arg_value(i, args, arg);
        } else if (arg == "--btrfs-uuid") {
            btrfs_uuid = arg_value(i, args, arg);
        } else if (arg == "--partition-uuid") {
            partition_uuid = arg_value(i, args, arg);
        } else if (arg == "--serial") {
            serial = arg_value(i, args, arg);
        } else if (arg == "--mapper-name") {
            mapper_name = arg_value(i, args, arg);
        } else if (arg == "--mount-point") {
            mount_point = arg_value(i, args, arg);
        } else if (arg == "--remote-root") {
            remote_root = arg_value(i, args, arg);
        } else if (arg == "--incoming-root") {
            incoming_root = arg_value(i, args, arg);
        } else if (arg == "--state-dir") {
            state_dir = arg_value(i, args, arg);
        } else if (arg == "--status-root") {
            status_root = arg_value(i, args, arg);
        } else if (arg == "--history-root") {
            history_root = arg_value(i, args, arg);
        } else if (arg == "--daily-limit") {
            daily_limit = arg_bool(arg_value(i, args, arg), arg);
        } else if (arg == "--incremental-required") {
            incremental_required = arg_bool(arg_value(i, args, arg), arg);
        } else if (arg == "--keep-failed-local-snapshot") {
            keep_failed_local_snapshot = arg_bool(arg_value(i, args, arg), arg);
        } else if (arg == "--auto-eject") {
            auto_eject = arg_bool(arg_value(i, args, arg), arg);
        } else if (arg == "--remote-retention") {
            remote_retention = arg_int(arg_value(i, args, arg), arg);
        } else if (arg == "--local-retention") {
            local_retention = arg_int(arg_value(i, args, arg), arg);
        } else if (arg == "--minimum-target-free-bytes") {
            minimum_target_free_bytes = arg_int(arg_value(i, args, arg), arg);
        } else if (arg == "--minimum-local-free-bytes") {
            minimum_local_free_bytes = arg_int(arg_value(i, args, arg), arg);
        } else if (arg == "--notify-enable") {
            notify_enabled = arg_bool(arg_value(i, args, arg), arg);
        } else if (arg == "--notify-user") {
            notify_user = arg_value(i, args, arg);
        } else if (arg == "--notify-method") {
            notify_method = arg_value(i, args, arg);
        } else if (arg == "--source") {
            if (i + 7 >= args.size()) {
                fail("--source requires ID NAME SUBVOLUME LOCAL_SNAPSHOT_DIR REMOTE_SUBDIR REMOTE_RETENTION LOCAL_RETENTION");
            }
            sources.push_back({
                {"id", args[++i]},
                {"name", args[++i]},
                {"enabled", true},
                {"subvolume", args[++i]},
                {"localSnapshotDir", args[++i]},
                {"remoteSubdir", args[++i]},
                {"remoteRetention", arg_int(args[++i], "--source remote retention")},
                {"localRetention", arg_int(args[++i], "--source local retention")}
            });
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: btrfs-backupctl profile create --output PATH [OPTIONS] --source ID NAME SUBVOLUME LOCAL_SNAPSHOT_DIR REMOTE_SUBDIR REMOTE_RETENTION LOCAL_RETENTION\n";
            return 0;
        } else {
            fail("unknown create option: " + arg);
        }
    }

    if (output.empty()) fail("create requires --output");
    if (device.empty()) fail("create requires --device");
    if (luks_uuid.empty()) fail("create requires --luks-uuid");
    if (mapper_name.empty()) fail("create requires --mapper-name");
    if (mount_point.empty()) fail("create requires --mount-point");
    if (remote_root.empty()) remote_root = mount_point + "/snapshots";
    if (incoming_root.empty()) incoming_root = mount_point + "/.incoming";
    if (sources.empty()) fail("create requires at least one --source");

    Profile profile = profile_from_json({
        {"schemaVersion", 1},
        {"profileId", profile_id},
        {"name", profile_name},
        {"enabled", true},
        {"target", {
            {"device", device},
            {"luksUuid", luks_uuid},
            {"btrfsUuid", btrfs_uuid},
            {"partitionUuid", partition_uuid},
            {"serial", serial},
            {"mapperName", mapper_name},
            {"mountPoint", mount_point}
        }},
        {"paths", {
            {"remoteRoot", remote_root},
            {"incomingRoot", incoming_root},
            {"stateDir", state_dir},
            {"statusRoot", status_root},
            {"historyRoot", history_root}
        }},
        {"settings", {
            {"dailyLimit", daily_limit},
            {"incrementalRequired", incremental_required},
            {"keepFailedLocalSnapshot", keep_failed_local_snapshot},
            {"autoEject", auto_eject},
            {"remoteRetention", remote_retention},
            {"localRetention", local_retention},
            {"minimumTargetFreeBytes", minimum_target_free_bytes},
            {"minimumLocalFreeBytes", minimum_local_free_bytes}
        }},
        {"notifications", {
            {"enabled", notify_enabled},
            {"user", notify_user},
            {"method", notify_method}
        }},
        {"sources", sources}
    });
    atomic_write(output, dump_json(profile_to_json(profile)), 0600);
    return 0;
}

void usage() {
    std::cout << "Usage: btrfs-backupctl profile [--etc-root PATH] [--udev-root PATH] [--public-root PATH] COMMAND\n"
              << "\nCommands:\n"
              << "  create --output PATH [OPTIONS]\n"
              << "  validate --file PATH\n"
              << "  render --file PATH --output-dir PATH\n"
              << "  save --file PATH\n"
              << "  show [--profile ID]\n"
              << "  export [--profile ID] --output PATH\n";
}

} // namespace

namespace btrfsbackup {

int command_profile(const std::vector<std::string>& args) {
    fs::path etc_root = std::getenv("BTRFS_BACKUP_ETC_ROOT") ? std::getenv("BTRFS_BACKUP_ETC_ROOT") : "/etc/btrfs-backup";
    fs::path udev_root = std::getenv("BTRFS_BACKUP_UDEV_ROOT") ? std::getenv("BTRFS_BACKUP_UDEV_ROOT") : "/etc/udev/rules.d";
    fs::path public_root = std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") ? std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") : "/var/lib/btrfs-backup/public/profiles";
    std::vector<std::string> rest;

    try {
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];
            if (arg == "--etc-root") {
                etc_root = arg_value(i, args, arg);
            } else if (arg == "--udev-root") {
                udev_root = arg_value(i, args, arg);
            } else if (arg == "--public-root") {
                public_root = arg_value(i, args, arg);
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                for (; i < args.size(); ++i) {
                    rest.emplace_back(args[i]);
                }
                break;
            }
        }
        if (rest.empty()) {
            usage();
            return 2;
        }
        std::string command = rest[0];
        if (command == "create") {
            return command_create_profile(std::vector<std::string>(rest.begin() + 1, rest.end()));
        }
        fs::path file;
        fs::path output_dir;
        std::string profile_id = "default";
        for (std::size_t i = 1; i < rest.size(); ++i) {
            const std::string& arg = rest[i];
            if (arg == "--file" && i + 1 < rest.size()) {
                file = rest[++i];
            } else if (arg == "--output-dir" && i + 1 < rest.size()) {
                output_dir = rest[++i];
            } else if (arg == "--profile" && i + 1 < rest.size()) {
                profile_id = rest[++i];
            } else if (arg == "--output" && i + 1 < rest.size()) {
                output_dir = rest[++i];
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                fail("unknown option: " + arg);
            }
        }

        if (command == "emit-runtime-env") {
            if (file.empty()) fail("emit-runtime-env requires --file");
            std::cout << render_profile_env(profile_from_json(load_json_file(file)));
        } else if (command == "validate") {
            if (file.empty()) fail("validate requires --file");
            std::cout << dump_json(profile_to_json(profile_from_json(load_json_file(file))));
        } else if (command == "render") {
            if (file.empty()) fail("render requires --file");
            if (output_dir.empty()) fail("render requires --output-dir");
            output_dir = fs::absolute(output_dir).lexically_normal();
            if (output_dir == "/" || output_dir == "/etc" || output_dir == "/usr" || output_dir == "/var") {
                throw ValidationError("refusing unsafe output directory: " + output_dir.string());
            }
            std::error_code ec;
            fs::remove_all(output_dir, ec);
            Profile profile = profile_from_json(load_json_file(file));
            render_tree(profile, output_dir);
            std::cout << "Rendered profile " << profile.id << " to " << output_dir << "\n";
        } else if (command == "save") {
            if (file.empty()) fail("save requires --file");
            if (geteuid() != 0 && etc_root == "/etc/btrfs-backup") {
                fail("save to system configuration must be run as root", 1);
            }
            Profile profile = profile_from_json(load_json_file(file));
            save_tree(profile, etc_root, udev_root, public_root);
            std::cout << "Saved profile " << profile.id << "\n";
        } else if (command == "show") {
            std::cout << dump_json(profile_to_json(load_profile_by_id(etc_root, profile_id)));
        } else if (command == "export") {
            if (output_dir.empty()) fail("export requires --output");
            Profile profile = load_profile_by_id(etc_root, profile_id);
            atomic_write(output_dir, dump_json(profile_to_json(profile)), 0600);
            std::cout << "Exported profile " << profile.id << " to " << output_dir << "\n";
        } else {
            fail("unknown command: " + command);
        }
    } catch (const ValidationError& exc) {
        fail(exc.what());
    } catch (const std::exception& exc) {
        fail(exc.what());
    }
    return 0;
}

} // namespace btrfsbackup
