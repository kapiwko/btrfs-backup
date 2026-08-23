#include <btrfsbackup/migrate_profile.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_store.hpp>
#include <btrfsbackup/shell_env.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {
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

void require_absolute(const fs::path& path, const std::string& name) {
    if (path.empty() || !path.is_absolute()) {
        throw ValidationError(name + " must be an absolute path");
    }
}

void require_single_line(const std::string& value, const std::string& name) {
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos || value.find('\t') != std::string::npos) {
        throw ValidationError(name + " must be a single-line value without tabs");
    }
}

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &tm);
    return buffer;
}

void assert_shell_input(const fs::path& path, bool system_write) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        throw ValidationError("Shell input file does not exist or is not a regular file: " + path.string());
    }
    if (access(path.c_str(), R_OK) != 0) {
        throw ValidationError("Shell input file is not readable: " + path.string());
    }
    if (system_write) {
        if ((st.st_mode & 0022) != 0) {
            throw ValidationError("Shell input must not be group/world writable: " + path.string());
        }
        return;
    }
    uid_t expected = geteuid();
    if (st.st_uid != expected) {
        throw ValidationError("Shell input must be owned by the invoking user: " + path.string());
    }
    if ((st.st_mode & 0077) != 0) {
        throw ValidationError("Shell input must be private (mode 0600 recommended): " + path.string());
    }
}

std::vector<fs::path> source_files_in(const fs::path& source_dir) {
    std::error_code ec;
    if (!fs::is_directory(source_dir, ec)) {
        throw ValidationError("No source definitions found in " + source_dir.string());
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(source_dir, ec)) {
        if (ec) {
            throw ValidationError("cannot read source definitions directory: " + source_dir.string());
        }
        if (entry.is_regular_file() && entry.path().extension() == ".conf") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        throw ValidationError("No source definitions found in " + source_dir.string());
    }
    return files;
}

Json source_from_env(const std::map<std::string, std::string>& env, long long remote_default, long long local_default) {
    if (!env_bool(env, "ENABLED", true)) {
        return Json();
    }
    std::string id = env_required(env, "SOURCE_NAME");
    std::string name = env_get(env, "SOURCE_DISPLAY_NAME", id);
    require_single_line(name, "SOURCE_DISPLAY_NAME");
    return {
        {"id", id},
        {"name", name},
        {"enabled", true},
        {"subvolume", env_required(env, "SOURCE_SUBVOLUME")},
        {"localSnapshotDir", env_required(env, "LOCAL_SNAPSHOT_DIR")},
        {"remoteSubdir", env_required(env, "REMOTE_SUBDIR")},
        {"remoteRetention", env_int(env, "SOURCE_RETENTION_COUNT", remote_default)},
        {"localRetention", env_int(env, "SOURCE_LOCAL_RETENTION_COUNT", local_default)}
    };
}

Profile build_profile(
    const std::map<std::string, std::string>& env,
    const fs::path& source_dir,
    const fs::path& profile_root,
    const std::string& profile_id,
    const std::string& profile_name,
    bool system_write
) {
    std::string mount = env_required(env, "BACKUP_MOUNTPOINT");
    long long remote_retention = env_int(env, "RETENTION_COUNT", 30);
    long long local_retention = env_int(env, "LOCAL_RETENTION_COUNT", remote_retention);

    Json sources = Json::array();
    for (const fs::path& file : source_files_in(source_dir)) {
        assert_shell_input(file, system_write);
        Json source = source_from_env(read_shell_environment(file), remote_retention, local_retention);
        if (!source.is_null()) {
            sources.push_back(source);
        }
    }
    if (sources.empty()) {
        throw ValidationError("No enabled source definitions found in " + source_dir.string());
    }

    std::string root = profile_root.string();
    std::string sources_dir = root + "/profiles/" + profile_id + "/sources.d";
    return profile_from_json({
        {"schemaVersion", 1},
        {"profileId", profile_id},
        {"name", profile_name},
        {"enabled", true},
        {"target", {
            {"device", env_required(env, "BACKUP_DEVICE")},
            {"luksUuid", env_required(env, "BACKUP_LUKS_UUID")},
            {"btrfsUuid", env_get(env, "BACKUP_BTRFS_UUID", "")},
            {"partitionUuid", env_get(env, "BACKUP_PARTITION_UUID", "")},
            {"serial", env_get(env, "BACKUP_SERIAL", "")},
            {"mapperName", env_required(env, "BACKUP_MAPPER_NAME")},
            {"mountPoint", mount}
        }},
        {"paths", {
            {"sourcesDir", sources_dir},
            {"remoteRoot", env_get(env, "REMOTE_ROOT", mount + "/snapshots")},
            {"incomingRoot", env_get(env, "INCOMING_ROOT", mount + "/.incoming")},
            {"stateDir", env_get(env, "STATE_DIR", "/var/lib/btrfs-backup")},
            {"statusRoot", env_get(env, "STATUS_ROOT", "/run/btrfs-backup/profiles")},
            {"historyRoot", env_get(env, "HISTORY_ROOT", "/var/lib/btrfs-backup/history")}
        }},
        {"settings", {
            {"dailyLimit", env_bool(env, "DAILY_LIMIT", true)},
            {"incrementalRequired", env_bool(env, "INCREMENTAL_REQUIRED", true)},
            {"keepFailedLocalSnapshot", env_bool(env, "KEEP_FAILED_LOCAL_SNAPSHOT", false)},
            {"autoEject", env_bool(env, "AUTO_EJECT", true)},
            {"remoteRetention", remote_retention},
            {"localRetention", local_retention},
            {"minimumTargetFreeBytes", env_int(env, "MIN_TARGET_FREE_BYTES", 5LL * 1024 * 1024 * 1024)},
            {"minimumLocalFreeBytes", env_int(env, "MIN_LOCAL_FREE_BYTES", 1024LL * 1024 * 1024)}
        }},
        {"notifications", {
            {"enabled", env_bool(env, "NOTIFY_ENABLE", true)},
            {"user", env_get(env, "NOTIFY_USER", "")},
            {"method", env_get(env, "NOTIFY_METHOD", "auto")}
        }},
        {"sources", sources}
    });
}

void backup_existing_file(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return;
    }
    fs::path backup = path.string() + ".backup-" + timestamp();
    fs::copy_file(path, backup, fs::copy_options::overwrite_existing);
    chmod(backup.c_str(), 0600);
}

void move_legacy_path_aside(const fs::path& path, const std::string& label) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return;
    }
    fs::path backup = path.string() + ".migrated-" + timestamp();
    fs::rename(path, backup);
    std::cout << "Moved legacy " << label << " aside: " << backup << '\n';
}

} // namespace

int command_migrate_profile(const std::vector<std::string>& args) {
    fs::path source_config = getenv_or("BTRFS_BACKUP_LEGACY_CONFIG", "/etc/btrfs-backup/backup.env");
    fs::path profile_config_dir = getenv_or("BTRFS_BACKUP_PROFILE_CONFIG_DIR", "/etc/btrfs-backup/profiles.d");
    fs::path source_config_dir;
    fs::path udev_rules_dir = getenv_or("BTRFS_BACKUP_UDEV_RULES_DIR", "/etc/udev/rules.d");
    fs::path public_profile_dir = getenv_or("BTRFS_BACKUP_PUBLIC_PROFILE_DIR", "/var/lib/btrfs-backup/public/profiles");
    std::string profile_id = getenv_or("BTRFS_BACKUP_PROFILE", "default");
    std::string profile_name = "Default backup";
    bool force = false;
    bool dry_run = false;
    bool remove_legacy = false;

    try {
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];
            if (arg == "--source") {
                source_config = arg_value(args, i, arg);
            } else if (arg == "--sources-dir") {
                source_config_dir = arg_value(args, i, arg);
            } else if (arg == "--profile") {
                profile_id = arg_value(args, i, arg);
            } else if (arg == "--name") {
                profile_name = arg_value(args, i, arg);
            } else if (arg == "--profile-dir") {
                profile_config_dir = arg_value(args, i, arg);
            } else if (arg == "--udev-dir") {
                udev_rules_dir = arg_value(args, i, arg);
            } else if (arg == "--public-dir") {
                public_profile_dir = arg_value(args, i, arg);
            } else if (arg == "--force") {
                force = true;
            } else if (arg == "--remove-legacy") {
                remove_legacy = true;
            } else if (arg == "--dry-run") {
                dry_run = true;
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                throw ValidationError("Unknown option: " + arg);
            }
        }

        validate_profile_id(profile_id);
        require_single_line(profile_name, "PROFILE_NAME");
        require_absolute(source_config, "SOURCE_CONFIG");
        require_absolute(profile_config_dir, "PROFILE_CONFIG_DIR");
        require_absolute(udev_rules_dir, "UDEV_RULES_DIR");
        require_absolute(public_profile_dir, "PUBLIC_PROFILE_DIR");
        if (!source_config_dir.empty()) {
            require_absolute(source_config_dir, "SOURCE_CONFIG_DIR");
        }

        fs::path profile_root = profile_config_dir.parent_path();
        fs::path target_profile_dir = profile_root / "profiles" / profile_id;
        fs::path target_profile_json = target_profile_dir / "profile.json";
        bool system_write =
            profile_root == "/etc/btrfs-backup" ||
            udev_rules_dir == "/etc/udev/rules.d" ||
            public_profile_dir == "/var/lib/btrfs-backup/public/profiles";

        if (dry_run) {
            if (!fs::is_regular_file(source_config)) {
                throw ValidationError("Legacy configuration does not exist: " + source_config.string());
            }
            std::cout << "Would create profile " << profile_id << " from " << source_config << '\n';
            std::cout << "Would create profile JSON at " << target_profile_json << '\n';
            return 0;
        }
        if (geteuid() != 0 && system_write) {
            fail("Writing system profile configuration requires root.", 1);
        }

        assert_shell_input(source_config, system_write);
        std::map<std::string, std::string> env = read_shell_environment(source_config);
        if (source_config_dir.empty()) {
            source_config_dir = env_get(env, "SOURCES_DIR", "/etc/btrfs-backup/sources.d");
        }
        require_absolute(source_config_dir, "SOURCE_CONFIG_DIR");

        if (fs::exists(target_profile_json) && !force) {
            throw ValidationError("Profile already exists: " + target_profile_json.string() + " (use --force to replace it)");
        }

        Profile profile = build_profile(env, source_config_dir, profile_root, profile_id, profile_name, system_write);
        backup_existing_file(target_profile_json);
        save_tree(profile, profile_root, udev_rules_dir, public_profile_dir);
        if (geteuid() == 0) {
            chown(target_profile_json.c_str(), 0, 0);
        }

        std::cout << "Created profile JSON: " << target_profile_json << '\n';

        if (remove_legacy) {
            fs::path legacy_udev_rule = udev_rules_dir / "99-btrfs-backup.rules";
            fs::path profile_udev_rule = udev_rules_dir / ("99-btrfs-backup-" + profile_id + ".rules");
            move_legacy_path_aside(source_config, "configuration");
            if (source_config_dir.lexically_normal() != fs::path(profile.paths.sources_dir).lexically_normal()) {
                move_legacy_path_aside(source_config_dir, "source directory");
            }
            if (legacy_udev_rule != profile_udev_rule) {
                move_legacy_path_aside(legacy_udev_rule, "udev rule");
            }
        }
    } catch (const ValidationError& exc) {
        fail(exc.what());
    } catch (const std::exception& exc) {
        fail(exc.what());
    }

    return 0;
}

} // namespace btrfsbackup
