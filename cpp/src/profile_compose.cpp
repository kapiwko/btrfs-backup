#include <btrfsbackup/profile_compose.hpp>

#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/profile.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

constexpr int schema_version = 1;

} // namespace

Profile profile_from_environment_sources(const fs::path& sources_table) {
    std::map<std::string, std::string> env;
    const std::vector<std::string> env_names{
        "PROFILE_ID", "PROFILE_NAME", "PROFILE_ROOT", "PROFILE_SOURCES_DIR",
        "BACKUP_DEVICE", "BACKUP_LUKS_UUID", "BACKUP_BTRFS_UUID",
        "BACKUP_PARTITION_UUID", "BACKUP_SERIAL", "BACKUP_MAPPER_NAME",
        "BACKUP_MOUNTPOINT", "REMOTE_ROOT", "INCOMING_ROOT", "STATE_DIR",
        "STATUS_ROOT", "HISTORY_ROOT", "RETENTION_COUNT", "LOCAL_RETENTION_COUNT",
        "DAILY_LIMIT", "INCREMENTAL_REQUIRED", "KEEP_FAILED_LOCAL_SNAPSHOT",
        "AUTO_EJECT", "MIN_TARGET_FREE_BYTES", "MIN_LOCAL_FREE_BYTES",
        "NOTIFY_ENABLE", "NOTIFY_USER", "NOTIFY_METHOD"
    };
    for (const auto& name : env_names) {
        if (const char* value = std::getenv(name.c_str())) {
            env[name] = value;
        }
    }

    std::string profile_id = env_required(env, "PROFILE_ID");
    std::string mount = env_required(env, "BACKUP_MOUNTPOINT");
    std::string profile_root = env_get(env, "PROFILE_ROOT", "/etc/btrfs-backup");
    std::string sources_dir = env_get(env, "PROFILE_SOURCES_DIR");
    if (sources_dir.empty()) {
        if (profile_root == "/etc/btrfs-backup") {
            sources_dir = "/etc/btrfs-backup/profiles/" + profile_id + "/sources.d";
        } else {
            sources_dir = profile_root + "/profiles/" + profile_id + "/sources.d";
        }
    }

    std::ifstream stream(sources_table);
    if (!stream) {
        throw ValidationError("cannot read sources table " + sources_table.string());
    }
    Json sources = Json::array();
    std::string line;
    while (std::getline(stream, line)) {
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true) {
            std::size_t tab = line.find('\t', start);
            if (tab == std::string::npos) {
                fields.push_back(line.substr(start));
                break;
            }
            fields.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (fields.size() == 6) {
            fields.insert(fields.begin() + 1, fields.at(0));
        }
        if (fields.size() != 7) {
            throw ValidationError("sources table must contain 6 or 7 tab-separated fields per line");
        }
        sources.push_back({
            {"id", fields.at(0)},
            {"name", fields.at(1)},
            {"enabled", true},
            {"subvolume", fields.at(2)},
            {"localSnapshotDir", fields.at(3)},
            {"remoteSubdir", fields.at(4)},
            {"remoteRetention", std::stoll(fields.at(5))},
            {"localRetention", std::stoll(fields.at(6))}
        });
    }

    return profile_from_json({
        {"schemaVersion", schema_version},
        {"profileId", profile_id},
        {"name", env_required(env, "PROFILE_NAME")},
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
            {"dailyLimit", env_bool(env, "DAILY_LIMIT", false)},
            {"incrementalRequired", env_bool(env, "INCREMENTAL_REQUIRED", false)},
            {"keepFailedLocalSnapshot", env_bool(env, "KEEP_FAILED_LOCAL_SNAPSHOT", false)},
            {"autoEject", env_bool(env, "AUTO_EJECT", false)},
            {"remoteRetention", env_int(env, "RETENTION_COUNT", 30)},
            {"localRetention", env_int(env, "LOCAL_RETENTION_COUNT", env_int(env, "RETENTION_COUNT", 30))},
            {"minimumTargetFreeBytes", env_int(env, "MIN_TARGET_FREE_BYTES", 5LL * 1024 * 1024 * 1024)},
            {"minimumLocalFreeBytes", env_int(env, "MIN_LOCAL_FREE_BYTES", 1024LL * 1024 * 1024)}
        }},
        {"notifications", {
            {"enabled", env_bool(env, "NOTIFY_ENABLE", false)},
            {"user", env_get(env, "NOTIFY_USER", "")},
            {"method", env_get(env, "NOTIFY_METHOD", "auto")}
        }},
        {"sources", sources}
    });
}

} // namespace btrfsbackup
