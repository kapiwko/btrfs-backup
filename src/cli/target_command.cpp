#include <cli/target_command.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl target: " << message << '\n';
    std::exit(code);
}

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        fail(option + " requires a value");
    }
    return args[++index];
}

struct TargetOptions {
    std::string profile_id = "default";
    bool force = false;
    bool from_service = false;
    bool from_runner = false;
};

TargetOptions parse_options(const std::vector<std::string>& args) {
    TargetOptions options;
    if (const char* env = std::getenv("BTRFS_BACKUP_PROFILE")) {
        options.profile_id = env;
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args.at(i);
        if (arg == "--profile") {
            options.profile_id = arg_value(args, i, arg);
        } else if (arg == "--force") {
            options.force = true;
        } else if (arg == "--from-service") {
            options.from_service = true;
        } else if (arg == "--from-runner") {
            options.from_runner = true;
        } else {
            fail("unknown option: " + arg);
        }
    }
    return options;
}

void usage(std::ostream& output) {
    output << "Usage: btrfs-backupctl target COMMAND\n"
           << "\nCommands:\n"
           << "  mount --profile ID\n"
           << "  eject --profile ID [--force] [--from-service] [--from-runner]\n";
}

bool service_succeeded() {
    const char* value = std::getenv("SERVICE_RESULT");
    return value == nullptr || std::string(value) == "success";
}

std::string format_event(const btrfsbackup::TargetEvent& event) {
    using btrfsbackup::TargetEventKind;
    switch (event.kind) {
        case TargetEventKind::AutomaticEjectDisabled:
            return "Automatic eject is disabled by configuration.";
        case TargetEventKind::Busy:
            return "Backup target is busy; refusing to " + event.detail + ".";
        case TargetEventKind::Mounting:
            return "Mounting encrypted backup target.";
        case TargetEventKind::Mounted:
            return "Backup target is mounted at " + event.detail + ".";
        case TargetEventKind::Synchronizing:
            return "Synchronizing filesystems before eject.";
        case TargetEventKind::Unmounting:
            return "Unmounting " + event.detail;
        case TargetEventKind::StoppingCryptUnit:
            return "Stopping LUKS systemd unit " + event.detail;
        case TargetEventKind::MapperStillMounted:
            return "Mapper is still mounted at: " + event.detail;
        case TargetEventKind::ClosingMapper:
            return "Closing LUKS mapper " + event.detail;
        case TargetEventKind::EjectedAfterFailedBackup:
            return "Backup did not finish successfully, but the target was unmounted and the LUKS mapper was closed. It can be disconnected.";
        case TargetEventKind::Ejected:
            return "Backup target was safely unmounted and the LUKS mapper was closed. It can be disconnected.";
    }
    return {};
}

void print_result(const btrfsbackup::TargetOperationResult& result, std::ostream& output) {
    for (const btrfsbackup::TargetEvent& event : result.events) {
        output << format_event(event) << '\n';
    }
}

} // namespace

namespace btrfsbackup::command {

int target(
    const fs::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    TargetExecutionServices* services
) {
    if (args.empty()) {
        usage(output);
        return 2;
    }
    const std::string& command = args.at(0);
    if (command == "-h" || command == "--help") {
        usage(output);
        return 0;
    }
    if (command != "mount" && command != "eject") {
        fail("unknown command: " + command);
    }
    if (args.size() == 2 && (args.at(1) == "-h" || args.at(1) == "--help")) {
        usage(output);
        return 0;
    }

    TargetOptions options = parse_options(args);
    TargetOperationResult result;
    if (command == "mount") {
        result = btrfsbackup::mount_target(
            MountTargetRequest{
                .profile_config_dir = profile_config_dir,
                .profile_id = ProfileId{options.profile_id},
            },
            services
        );
    } else {
        result = btrfsbackup::eject_target(
            EjectTargetRequest{
                .profile_config_dir = profile_config_dir,
                .profile_id = ProfileId{options.profile_id},
                .force = options.force,
                .automatic = options.from_service || options.from_runner,
                .service_succeeded = !options.from_service || service_succeeded(),
            },
            services
        );
    }
    print_result(result, output);
    return result.busy ? 1 : 0;
}

int target(const fs::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output) {
    return target(profile_config_dir, args, output, nullptr);
}

} // namespace btrfsbackup::command
