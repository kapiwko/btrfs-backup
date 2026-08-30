// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/RunnerOptions.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace btrfsbackup::cli {
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

std::pair<RunnerCommandKind, std::string> parse_command(const std::string& command) {
    if (command == "plan") {
        return {RunnerCommandKind::Plan, command};
    }
    if (command == "execute") {
        return {RunnerCommandKind::Execute, command};
    }
    if (command == "cancel") {
        return {RunnerCommandKind::Cancel, command};
    }
    fail("unknown command: " + command);
}

} // namespace

RunnerOptions parse_runner_options(const std::vector<std::string>& args) {
    if (args.empty()) {
        fail("command is required");
    }

    const auto [command, command_name] = parse_command(args.front());
    const RuntimeTimePoint initial_timestamp = std::chrono::system_clock::now();
    btrfsbackup::backup::BackupRequest request{.profile_id = ProfileId{"default"}};
    std::filesystem::path mountinfo = "/proc/self/mountinfo";
    std::map<std::string, std::string> mount_uuid_overrides;
    RuntimeTimePoint timestamp = initial_timestamp;
    LocalDate today = local_date_at(initial_timestamp);
    std::optional<RunId> run_id;
    bool target_mode_selected = false;
    bool mount_target = false;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args.at(i);
        if (arg == "--profile") {
            request.profile_id = ProfileId{arg_value(args, i, arg)};
        } else if (arg == "--timestamp") {
            const std::string value = arg_value(args, i, arg);
            const auto parsed_timestamp = parse_utc_timestamp(value);
            if (!parsed_timestamp.has_value()) {
                fail("--timestamp must be a valid UTC timestamp");
            }
            timestamp = *parsed_timestamp;
        } else if (arg == "--run-id") {
            run_id = RunId{arg_value(args, i, arg)};
        } else if (arg == "--today") {
            const std::string value = arg_value(args, i, arg);
            const auto parsed_date = parse_local_date(value);
            if (!parsed_date.has_value()) {
                fail("--today must be a valid local date");
            }
            today = *parsed_date;
        } else if (arg == "--mountinfo") {
            mountinfo = arg_value(args, i, arg);
        } else if (arg == "--mount-uuid") {
            std::string source = arg_value(args, i, arg);
            mount_uuid_overrides[source] = arg_value(args, i, arg);
        } else if (arg == "--force") {
            request.force = true;
        } else if (arg == "--validate") {
            request.validate_only = true;
        } else if (arg == "--offline" || arg == "--mount-target") {
            if (command != RunnerCommandKind::Plan) {
                fail(arg + " is only valid for plan");
            }
            if (target_mode_selected) {
                fail("--offline and --mount-target are mutually exclusive");
            }
            target_mode_selected = true;
            mount_target = arg == "--mount-target";
        } else {
            fail("unknown " + command_name + " option: " + arg);
        }
    }

    if (command == RunnerCommandKind::Cancel && !run_id.has_value()) {
        fail("--run-id is required for cancel");
    }
    if (!run_id.has_value()) {
        std::string compact = format_utc_snapshot_timestamp(timestamp);
        compact.erase(4, 1);
        compact.erase(6, 1);
        run_id = RunId{compact + "-shadow"};
    }

    return {
        .command = command,
        .request = std::move(request),
        .mount_target = mount_target,
        .mountinfo = std::move(mountinfo),
        .mount_uuid_overrides = std::move(mount_uuid_overrides),
        .timestamp = timestamp,
        .today = today,
        .run_id = std::move(*run_id),
    };
}

} // namespace btrfsbackup::cli
