#include <btrfsbackup/profile_migrate.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_store.hpp>
#include <btrfsbackup/shell_env.hpp>

namespace fs = std::filesystem;

namespace {

void require_absolute(const fs::path& path, const std::string& name) {
    if (path.empty() || !path.is_absolute()) {
        throw btrfsbackup::ValidationError(name + " must be an absolute path");
    }
}

void require_single_line(const std::string& value, const std::string& name) {
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos || value.find('\t') != std::string::npos) {
        throw btrfsbackup::ValidationError(name + " must be a single-line value without tabs");
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
        throw btrfsbackup::ValidationError("Shell input file does not exist or is not a regular file: " + path.string());
    }
    if (access(path.c_str(), R_OK) != 0) {
        throw btrfsbackup::ValidationError("Shell input file is not readable: " + path.string());
    }
    if (system_write) {
        if ((st.st_mode & 0022) != 0) {
            throw btrfsbackup::ValidationError("Shell input must not be group/world writable: " + path.string());
        }
        return;
    }
    uid_t expected = geteuid();
    if (st.st_uid != expected) {
        throw btrfsbackup::ValidationError("Shell input must be owned by the invoking user: " + path.string());
    }
    if ((st.st_mode & 0077) != 0) {
        throw btrfsbackup::ValidationError("Shell input must be private (mode 0600 recommended): " + path.string());
    }
}

std::vector<fs::path> source_files_in(const fs::path& source_dir) {
    std::error_code ec;
    if (!fs::is_directory(source_dir, ec)) {
        throw btrfsbackup::ValidationError("No source definitions found in " + source_dir.string());
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(source_dir, ec)) {
        if (ec) {
            throw btrfsbackup::ValidationError("cannot read source definitions directory: " + source_dir.string());
        }
        if (entry.is_regular_file() && entry.path().extension() == ".conf") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        throw btrfsbackup::ValidationError("No source definitions found in " + source_dir.string());
    }
    return files;
}

btrfsbackup::Json source_from_env(const std::map<std::string, std::string>& env, long long remote_default, long long local_default) {
    if (!btrfsbackup::env_bool(env, "ENABLED", true)) {
        return btrfsbackup::Json();
    }
    std::string id = btrfsbackup::env_required(env, "SOURCE_NAME");
    std::string name = btrfsbackup::env_get(env, "SOURCE_DISPLAY_NAME", id);
    require_single_line(name, "SOURCE_DISPLAY_NAME");
    return {
        {"id", id},
        {"name", name},
        {"enabled", true},
        {"subvolume", btrfsbackup::env_required(env, "SOURCE_SUBVOLUME")},
        {"localSnapshotDir", btrfsbackup::env_required(env, "LOCAL_SNAPSHOT_DIR")},
        {"remoteSubdir", btrfsbackup::env_required(env, "REMOTE_SUBDIR")},
        {"remoteRetention", btrfsbackup::env_int(env, "SOURCE_RETENTION_COUNT", remote_default)},
        {"localRetention", btrfsbackup::env_int(env, "SOURCE_LOCAL_RETENTION_COUNT", local_default)}
    };
}

btrfsbackup::Profile build_profile(
    const std::map<std::string, std::string>& env,
    const fs::path& source_dir,
    const fs::path& profile_root,
    const std::string& profile_id,
    const std::string& profile_name,
    bool system_write
) {
    std::string mount = btrfsbackup::env_required(env, "BACKUP_MOUNTPOINT");
    long long remote_retention = btrfsbackup::env_int(env, "RETENTION_COUNT", 30);
    long long local_retention = btrfsbackup::env_int(env, "LOCAL_RETENTION_COUNT", remote_retention);

    btrfsbackup::Json sources = btrfsbackup::Json::array();
    for (const fs::path& file : source_files_in(source_dir)) {
        assert_shell_input(file, system_write);
        btrfsbackup::Json source = source_from_env(btrfsbackup::read_shell_environment(file), remote_retention, local_retention);
        if (!source.is_null()) {
            sources.push_back(source);
        }
    }
    if (sources.empty()) {
        throw btrfsbackup::ValidationError("No enabled source definitions found in " + source_dir.string());
    }

    std::string root = profile_root.string();
    std::string sources_dir = root + "/profiles/" + profile_id + "/sources.d";
    return btrfsbackup::profile_from_json({
        {"schemaVersion", 1},
        {"profileId", profile_id},
        {"name", profile_name},
        {"enabled", true},
        {"target", {
            {"device", btrfsbackup::env_required(env, "BACKUP_DEVICE")},
            {"luksUuid", btrfsbackup::env_required(env, "BACKUP_LUKS_UUID")},
            {"btrfsUuid", btrfsbackup::env_get(env, "BACKUP_BTRFS_UUID", "")},
            {"partitionUuid", btrfsbackup::env_get(env, "BACKUP_PARTITION_UUID", "")},
            {"serial", btrfsbackup::env_get(env, "BACKUP_SERIAL", "")},
            {"mapperName", btrfsbackup::env_required(env, "BACKUP_MAPPER_NAME")},
            {"mountPoint", mount}
        }},
        {"paths", {
            {"sourcesDir", sources_dir},
            {"remoteRoot", btrfsbackup::env_get(env, "REMOTE_ROOT", mount + "/snapshots")},
            {"incomingRoot", btrfsbackup::env_get(env, "INCOMING_ROOT", mount + "/.incoming")},
            {"stateDir", btrfsbackup::env_get(env, "STATE_DIR", "/var/lib/btrfs-backup")},
            {"statusRoot", btrfsbackup::env_get(env, "STATUS_ROOT", "/run/btrfs-backup/profiles")},
            {"historyRoot", btrfsbackup::env_get(env, "HISTORY_ROOT", "/var/lib/btrfs-backup/history")}
        }},
        {"settings", {
            {"dailyLimit", btrfsbackup::env_bool(env, "DAILY_LIMIT", true)},
            {"incrementalRequired", btrfsbackup::env_bool(env, "INCREMENTAL_REQUIRED", true)},
            {"keepFailedLocalSnapshot", btrfsbackup::env_bool(env, "KEEP_FAILED_LOCAL_SNAPSHOT", false)},
            {"autoEject", btrfsbackup::env_bool(env, "AUTO_EJECT", true)},
            {"remoteRetention", remote_retention},
            {"localRetention", local_retention},
            {"minimumTargetFreeBytes", btrfsbackup::env_int(env, "MIN_TARGET_FREE_BYTES", 5LL * 1024 * 1024 * 1024)},
            {"minimumLocalFreeBytes", btrfsbackup::env_int(env, "MIN_LOCAL_FREE_BYTES", 1024LL * 1024 * 1024)}
        }},
        {"notifications", {
            {"enabled", btrfsbackup::env_bool(env, "NOTIFY_ENABLE", true)},
            {"user", btrfsbackup::env_get(env, "NOTIFY_USER", "")},
            {"method", btrfsbackup::env_get(env, "NOTIFY_METHOD", "auto")}
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

void move_legacy_path_aside(const fs::path& path, const std::string& label, std::ostream& output) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return;
    }
    fs::path backup = path.string() + ".migrated-" + timestamp();
    fs::rename(path, backup);
    output << "Moved legacy " << label << " aside: " << backup << '\n';
}

bool system_write_for(const btrfsbackup::ProfileMigrationOptions& options) {
    fs::path profile_root = options.profile_config_dir.parent_path();
    return profile_root == "/etc/btrfs-backup" ||
        options.udev_rules_dir == "/etc/udev/rules.d" ||
        options.public_profile_dir == "/var/lib/btrfs-backup/public/profiles";
}

void validate_options(const btrfsbackup::ProfileMigrationOptions& options) {
    btrfsbackup::validate_profile_id(options.profile_id);
    require_single_line(options.profile_name, "PROFILE_NAME");
    require_absolute(options.source_config, "SOURCE_CONFIG");
    require_absolute(options.profile_config_dir, "PROFILE_CONFIG_DIR");
    require_absolute(options.udev_rules_dir, "UDEV_RULES_DIR");
    require_absolute(options.public_profile_dir, "PUBLIC_PROFILE_DIR");
    if (!options.source_config_dir.empty()) {
        require_absolute(options.source_config_dir, "SOURCE_CONFIG_DIR");
    }
}

} // namespace

namespace btrfsbackup {

bool profile_migration_requires_root(const ProfileMigrationOptions& options) {
    return !options.dry_run && system_write_for(options);
}

void execute_profile_migration(const ProfileMigrationOptions& options, std::ostream& output) {
    validate_options(options);

    fs::path profile_root = options.profile_config_dir.parent_path();
    fs::path target_profile_dir = profile_root / "profiles" / options.profile_id;
    fs::path target_profile_json = target_profile_dir / "profile.json";
    bool system_write = system_write_for(options);

    if (options.dry_run) {
        if (!fs::is_regular_file(options.source_config)) {
            throw ValidationError("Legacy configuration does not exist: " + options.source_config.string());
        }
        output << "Would create profile " << options.profile_id << " from " << options.source_config << '\n';
        output << "Would create profile JSON at " << target_profile_json << '\n';
        return;
    }

    fs::path source_config_dir = options.source_config_dir;
    assert_shell_input(options.source_config, system_write);
    std::map<std::string, std::string> env = read_shell_environment(options.source_config);
    if (source_config_dir.empty()) {
        source_config_dir = env_get(env, "SOURCES_DIR", "/etc/btrfs-backup/sources.d");
    }
    require_absolute(source_config_dir, "SOURCE_CONFIG_DIR");

    if (fs::exists(target_profile_json) && !options.force) {
        throw ValidationError("Profile already exists: " + target_profile_json.string() + " (use --force to replace it)");
    }

    Profile profile = build_profile(env, source_config_dir, profile_root, options.profile_id, options.profile_name, system_write);
    backup_existing_file(target_profile_json);
    save_tree(profile, profile_root, options.udev_rules_dir, options.public_profile_dir);
    if (geteuid() == 0) {
        chown(target_profile_json.c_str(), 0, 0);
    }

    output << "Created profile JSON: " << target_profile_json << '\n';

    if (options.remove_legacy) {
        fs::path legacy_udev_rule = options.udev_rules_dir / "99-btrfs-backup.rules";
        fs::path profile_udev_rule = options.udev_rules_dir / ("99-btrfs-backup-" + options.profile_id + ".rules");
        move_legacy_path_aside(options.source_config, "configuration", output);
        if (source_config_dir.lexically_normal() != fs::path(profile.paths.sources_dir).lexically_normal()) {
            move_legacy_path_aside(source_config_dir, "source directory", output);
        }
        if (legacy_udev_rule != profile_udev_rule) {
            move_legacy_path_aside(legacy_udev_rule, "udev rule", output);
        }
    }
}

} // namespace btrfsbackup
