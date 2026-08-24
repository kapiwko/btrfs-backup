#include <btrfsbackup/cli/command/target_command.hpp>

#include <unistd.h>

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <btrfsbackup/system/device_info.hpp>
#include <btrfsbackup/model/errors.hpp>
#include <btrfsbackup/system/file_lock.hpp>
#include <btrfsbackup/system/profile_loader.hpp>
#include <btrfsbackup/system/target_mount_validation.hpp>
#include <btrfsbackup/model/validation.hpp>

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

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool rootless_tests_allowed() {
    const char* value = std::getenv("BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS");
    return value != nullptr && std::string(value) == "true";
}

void require_root() {
    if (geteuid() != 0 && !rootless_tests_allowed()) {
        throw btrfsbackup::ValidationError("target command must be run as root");
    }
}

void info(std::ostream& output, const std::string& message) {
    output << message << '\n';
}

std::string capture(btrfsbackup::ICommandRunner& commands, const std::vector<std::string>& argv) {
    btrfsbackup::CommandResult result = commands.run(argv);
    if (result.exit_code != 0) {
        throw btrfsbackup::ValidationError("command failed: " + argv.front());
    }
    while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r')) {
        result.output.pop_back();
    }
    return result.output;
}

void run_checked(btrfsbackup::ICommandRunner& commands, const std::vector<std::string>& argv, const std::string& message) {
    btrfsbackup::CommandResult result = commands.run(argv);
    if (result.exit_code != 0) {
        throw btrfsbackup::ValidationError(message);
    }
}

void run_ignored(btrfsbackup::ICommandRunner& commands, const std::vector<std::string>& argv) {
    (void)commands.run(argv);
}

std::string cryptsetup_unit_name(btrfsbackup::ICommandRunner& commands, const std::string& mapper_name) {
    return capture(commands, {"systemd-escape", "--template=systemd-cryptsetup@.service", mapper_name});
}

void validate_luks_uuid(btrfsbackup::ICommandRunner& commands, const btrfsbackup::Profile& profile) {
    std::string actual = capture(commands, {"cryptsetup", "luksUUID", profile.target.device});
    if (actual.empty() || lower(actual) != lower(profile.target.luks_uuid)) {
        throw btrfsbackup::ValidationError("LUKS UUID mismatch for " + profile.target.device);
    }
}

std::string mapper_underlying_device(btrfsbackup::ICommandRunner& commands, const std::string& mapper_name) {
    std::string status = capture(commands, {"cryptsetup", "status", mapper_name});
    std::string marker = "device:";
    std::size_t pos = status.find(marker);
    if (pos == std::string::npos) {
        return {};
    }
    pos += marker.size();
    while (pos < status.size() && std::isspace(static_cast<unsigned char>(status[pos]))) {
        ++pos;
    }
    std::size_t end = pos;
    while (end < status.size() && !std::isspace(static_cast<unsigned char>(status[end]))) {
        ++end;
    }
    return status.substr(pos, end - pos);
}

bool mapper_identity_matches(btrfsbackup::ICommandRunner& commands, const btrfsbackup::Profile& profile) {
    fs::path configured = btrfsbackup::canonical_device(profile.target.device);
    fs::path actual = btrfsbackup::canonical_device(mapper_underlying_device(commands, profile.target.mapper_name));
    if (configured.empty() || actual.empty() || configured != actual) {
        return false;
    }
    try {
        validate_luks_uuid(commands, profile);
    } catch (const btrfsbackup::ValidationError&) {
        return false;
    }
    return true;
}

bool mapper_has_mounts(
    const btrfsbackup::Profile& profile,
    const std::vector<btrfsbackup::MountEntry>& mounts,
    std::ostream& output
) {
    fs::path mapper = fs::path("/dev/mapper") / profile.target.mapper_name;
    for (const btrfsbackup::MountEntry& mount : mounts) {
        if (btrfsbackup::normalized_path(btrfsbackup::strip_subvolume_suffix(mount.source)) == btrfsbackup::normalized_path(mapper)) {
            info(output, "Mapper is still mounted at: " + mount.target);
            return true;
        }
    }
    return false;
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

void mount_target(
    const btrfsbackup::Profile& profile,
    const TargetOptions&,
    btrfsbackup::ICommandRunner& commands,
    const std::function<std::vector<btrfsbackup::MountEntry>()>& read_mounts,
    std::ostream& output
) {
    require_root();
    validate_luks_uuid(commands, profile);

    fs::create_directories(profile.target.mount_point);
    chmod(profile.target.mount_point.c_str(), 0755);

    if (!btrfsbackup::mount_at(read_mounts(), profile.target.mount_point).has_value()) {
        info(output, "Mounting encrypted backup target.");
        run_checked(commands, {"systemctl", "start", profile.target.mount_unit}, "could not start target mount unit " + profile.target.mount_unit);
    }

    btrfsbackup::validate_target_mount(profile, read_mounts());
    info(output, "Backup target is mounted at " + profile.target.mount_point + ".");
}

void eject_target(
    const btrfsbackup::Profile& profile,
    const TargetOptions& options,
    btrfsbackup::ICommandRunner& commands,
    const std::function<std::vector<btrfsbackup::MountEntry>()>& read_mounts,
    std::ostream& output
) {
    require_root();
    if ((options.from_service || options.from_runner) && !profile.settings.auto_eject) {
        info(output, "Automatic eject is disabled by configuration.");
        return;
    }

    info(output, "Synchronizing filesystems before eject.");
    run_checked(commands, {"sync"}, "sync failed");

    std::vector<btrfsbackup::MountEntry> mounts = read_mounts();
    if (btrfsbackup::mount_at(mounts, profile.target.mount_point).has_value()) {
        if (!options.force && !btrfsbackup::mount_uses_mapper(mounts, profile.target.mount_point, fs::path("/dev/mapper") / profile.target.mapper_name)) {
            throw btrfsbackup::ValidationError("Refusing to unmount " + profile.target.mount_point + " because it is not backed by /dev/mapper/" + profile.target.mapper_name);
        }
        info(output, "Unmounting " + profile.target.mount_point);
        run_checked(commands, {"umount", "--", profile.target.mount_point}, "could not unmount " + profile.target.mount_point);
    }

    std::string crypt_unit = cryptsetup_unit_name(commands, profile.target.mapper_name);
    info(output, "Stopping LUKS systemd unit " + crypt_unit);
    run_ignored(commands, {"systemctl", "stop", crypt_unit});

    fs::path mapper = fs::path("/dev/mapper") / profile.target.mapper_name;
    if (fs::exists(mapper)) {
        if (!options.force && !mapper_identity_matches(commands, profile)) {
            throw btrfsbackup::ValidationError("Refusing to close mapper " + profile.target.mapper_name + " because its underlying device does not match configuration.");
        }
        if (mapper_has_mounts(profile, read_mounts(), output)) {
            throw btrfsbackup::ValidationError("Refusing to close mapper " + profile.target.mapper_name + " while it still has mounted filesystems.");
        }
        info(output, "Closing LUKS mapper " + profile.target.mapper_name);
        run_checked(commands, {"cryptsetup", "close", profile.target.mapper_name}, "could not close mapper " + profile.target.mapper_name);
    }

    if (fs::exists(profile.target.device)) {
        run_ignored(commands, {"blockdev", "--flushbufs", profile.target.device});
    }
    run_ignored(commands, {"udevadm", "settle", "--timeout=10"});

    const char* service_result = std::getenv("SERVICE_RESULT");
    if (options.from_service && service_result != nullptr && std::string(service_result) != "success") {
        info(output, "Backup did not finish successfully, but the target was unmounted and the LUKS mapper was closed. It can be disconnected.");
    } else {
        info(output, "Backup target was safely unmounted and the LUKS mapper was closed. It can be disconnected.");
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
    std::string command = args.at(0);
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
    Profile profile = load_profile_by_id(profile_config_dir, options.profile_id);
    require_root();
    SystemCommandRunner real_commands;
    ICommandRunner& commands = services == nullptr ? static_cast<ICommandRunner&>(real_commands) : services->commands;
    auto read_mounts = services == nullptr || !services->read_mounts
        ? std::function<std::vector<MountEntry>()>([] { return read_mount_table(); })
        : services->read_mounts;

    if (command == "eject" && (options.from_service || options.from_runner) && !profile.settings.auto_eject) {
        info(output, "Automatic eject is disabled by configuration.");
        return 0;
    }

    const fs::path lock_root = services != nullptr && !services->lock_root.empty()
        ? services->lock_root
        : default_lock_root();
    FileLock target_lock(target_lock_path(lock_root, profile.target.luks_uuid));
    if (!target_lock.try_acquire()) {
        info(output, "Backup target is busy; refusing to " + command + ".");
        return 1;
    }

    if (command == "mount") {
        mount_target(profile, options, commands, read_mounts, output);
    } else {
        eject_target(profile, options, commands, read_mounts, output);
    }
    return 0;
}

int target(const fs::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output) {
    return target(profile_config_dir, args, output, nullptr);
}

} // namespace btrfsbackup::command
