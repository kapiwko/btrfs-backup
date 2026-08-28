// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/profile_create_command.hpp>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <platform/linux/config/profile_service.hpp>
#include <config/model/json.hpp>
#include <config/model/json_io.hpp>
#include <config/model/profile.hpp>
#include <config/model/profile_document.hpp>

namespace fs = std::filesystem;
using btrfsbackup::config::dump_json;
using btrfsbackup::config::Json;
using btrfsbackup::config::Profile;
using btrfsbackup::config::profile_from_json;
using btrfsbackup::config::profile_to_json;

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

std::uint64_t arg_uint(const std::string& value, const std::string& option) {
    try {
        std::size_t pos = 0;
        const std::uint64_t result = std::stoull(value, &pos);
        if (pos != value.size() || value.starts_with('-')) {
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

void usage() {
    std::cout << "Usage: btrfs-backupctl profile create --output PATH [OPTIONS] --source ID NAME SUBVOLUME LOCAL_SNAPSHOT_DIR REMOTE_SUBDIR REMOTE_RETENTION LOCAL_RETENTION\n";
}

} // namespace

namespace btrfsbackup::cli {

int profile_create(const std::vector<std::string>& args) {
    fs::path output;
    std::string profile_id = "default";
    std::string profile_name = "Default backup";
    std::string device;
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string partition_uuid;
    std::string serial;
    std::string mapper_name;
    std::string remote_root;
    std::string incoming_root;
    bool daily_limit = true;
    bool incremental_required = true;
    bool keep_failed_local_snapshot = false;
    bool auto_eject = true;
    std::uint64_t remote_retention = 30;
    std::uint64_t local_retention = 30;
    std::uint64_t minimum_target_free_bytes = 5368709120ULL;
    std::uint64_t minimum_local_free_bytes = 1073741824ULL;
    btrfsbackup::config::Json sources = btrfsbackup::config::Json::array();

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
        } else if (arg == "--remote-root") {
            remote_root = arg_value(i, args, arg);
        } else if (arg == "--incoming-root") {
            incoming_root = arg_value(i, args, arg);
        } else if (arg == "--daily-limit") {
            daily_limit = arg_bool(arg_value(i, args, arg), arg);
        } else if (arg == "--incremental-required") {
            incremental_required = arg_bool(arg_value(i, args, arg), arg);
        } else if (arg == "--keep-failed-local-snapshot") {
            keep_failed_local_snapshot = arg_bool(arg_value(i, args, arg), arg);
        } else if (arg == "--auto-eject") {
            auto_eject = arg_bool(arg_value(i, args, arg), arg);
        } else if (arg == "--remote-retention") {
            remote_retention = arg_uint(arg_value(i, args, arg), arg);
        } else if (arg == "--local-retention") {
            local_retention = arg_uint(arg_value(i, args, arg), arg);
        } else if (arg == "--minimum-target-free-bytes") {
            minimum_target_free_bytes = arg_uint(arg_value(i, args, arg), arg);
        } else if (arg == "--minimum-local-free-bytes") {
            minimum_local_free_bytes = arg_uint(arg_value(i, args, arg), arg);
        } else if (arg == "--source") {
            if (i + 7 >= args.size()) {
                fail("--source requires ID NAME SUBVOLUME LOCAL_SNAPSHOT_DIR REMOTE_SUBDIR REMOTE_RETENTION LOCAL_RETENTION");
            }
            sources.push_back({{"id", args[++i]}, {"name", args[++i]}, {"enabled", true}, {"subvolume", args[++i]}, {"localSnapshotDir", args[++i]}, {"remoteSubdir", args[++i]}, {"remoteRetention", arg_uint(args[++i], "--source remote retention")}, {"localRetention", arg_uint(args[++i], "--source local retention")}});
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else {
            fail("unknown create option: " + arg);
        }
    }

    if (output.empty())
        fail("create requires --output");
    if (device.empty())
        fail("create requires --device");
    if (luks_uuid.empty())
        fail("create requires --luks-uuid");
    if (mapper_name.empty())
        fail("create requires --mapper-name");
    if (sources.empty())
        fail("create requires at least one --source");

    btrfsbackup::config::Json paths = btrfsbackup::config::Json::object();
    if (!remote_root.empty())
        paths["remoteRoot"] = remote_root;
    if (!incoming_root.empty())
        paths["incomingRoot"] = incoming_root;

    btrfsbackup::config::Profile profile = btrfsbackup::config::profile_from_json({{"schemaVersion", btrfsbackup::config::current_profile_schema_version}, {"profileId", profile_id}, {"name", profile_name}, {"enabled", true}, {"target", {{"device", device}, {"luksUuid", luks_uuid}, {"btrfsUuid", btrfs_uuid}, {"partitionUuid", partition_uuid}, {"serial", serial}, {"mapperName", mapper_name}}}, {"paths", paths}, {"settings", {{"dailyLimit", daily_limit}, {"incrementalRequired", incremental_required}, {"keepFailedLocalSnapshot", keep_failed_local_snapshot}, {"autoEject", auto_eject}, {"remoteRetention", remote_retention}, {"localRetention", local_retention}, {"minimumTargetFreeBytes", minimum_target_free_bytes}, {"minimumLocalFreeBytes", minimum_local_free_bytes}}}, {"sources", sources}});
    btrfsbackup::platform::linux::write_profile_file(profile, output);
    return 0;
}

} // namespace btrfsbackup::cli
