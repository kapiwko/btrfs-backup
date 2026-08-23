#include <btrfsbackup/command/profile_migrate_command.hpp>

#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/profile_migrate.hpp>

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl profile migrate: " << message << '\n';
    std::exit(code);
}

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        fail(option + " requires a value");
    }
    return args[++index];
}

void usage() {
    std::cout << "Usage: btrfs-backupctl profile migrate [options]\n"
              << "\nOptions:\n"
              << "  --source PATH       Legacy configuration file (default: /etc/btrfs-backup/backup.env).\n"
              << "  --sources-dir PATH  Legacy source definitions directory (default: SOURCES_DIR from source).\n"
              << "  --profile ID        Profile id to create (default: default).\n"
              << "  --name TEXT         Human-readable profile name (default: Default backup).\n"
              << "  --profile-dir PATH  Profile directory (default: /etc/btrfs-backup/profiles.d).\n"
              << "  --udev-dir PATH     udev rules directory (default: /etc/udev/rules.d).\n"
              << "  --public-dir PATH   Public profile manifest directory.\n"
              << "  --force             Replace an existing profile file after saving a timestamped backup.\n"
              << "  --remove-legacy     Move legacy configuration, source directory, and udev rule aside.\n"
              << "  --dry-run           Validate inputs and print the target path without writing.\n"
              << "  -h, --help          Show this help.\n";
}

std::string getenv_or(const char* name, const char* default_value) {
    if (const char* value = std::getenv(name)) {
        return value;
    }
    return default_value;
}

btrfsbackup::ProfileMigrationOptions default_options() {
    return {
        .source_config = getenv_or("BTRFS_BACKUP_LEGACY_CONFIG", "/etc/btrfs-backup/backup.env"),
        .profile_config_dir = getenv_or("BTRFS_BACKUP_PROFILE_CONFIG_DIR", "/etc/btrfs-backup/profiles.d"),
        .source_config_dir = {},
        .udev_rules_dir = getenv_or("BTRFS_BACKUP_UDEV_RULES_DIR", "/etc/udev/rules.d"),
        .public_profile_dir = getenv_or("BTRFS_BACKUP_PUBLIC_PROFILE_DIR", "/var/lib/btrfs-backup/public/profiles"),
        .profile_id = getenv_or("BTRFS_BACKUP_PROFILE", "default"),
        .profile_name = "Default backup",
    };
}

} // namespace

namespace btrfsbackup::command {

int profile_migrate(const std::vector<std::string>& args) {
    ProfileMigrationOptions options = default_options();

    try {
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];
            if (arg == "--source") {
                options.source_config = arg_value(args, i, arg);
            } else if (arg == "--sources-dir") {
                options.source_config_dir = arg_value(args, i, arg);
            } else if (arg == "--profile") {
                options.profile_id = arg_value(args, i, arg);
            } else if (arg == "--name") {
                options.profile_name = arg_value(args, i, arg);
            } else if (arg == "--profile-dir") {
                options.profile_config_dir = arg_value(args, i, arg);
            } else if (arg == "--udev-dir") {
                options.udev_rules_dir = arg_value(args, i, arg);
            } else if (arg == "--public-dir") {
                options.public_profile_dir = arg_value(args, i, arg);
            } else if (arg == "--force") {
                options.force = true;
            } else if (arg == "--remove-legacy") {
                options.remove_legacy = true;
            } else if (arg == "--dry-run") {
                options.dry_run = true;
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                throw ValidationError("Unknown option: " + arg);
            }
        }

        if (profile_migration_requires_root(options) && geteuid() != 0) {
            fail("Writing system profile configuration requires root.", 1);
        }
        execute_profile_migration(options, std::cout);
    } catch (const ValidationError& exc) {
        fail(exc.what());
    } catch (const std::exception& exc) {
        fail(exc.what());
    }

    return 0;
}

} // namespace btrfsbackup::command
