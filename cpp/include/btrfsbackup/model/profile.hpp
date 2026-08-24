#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <btrfsbackup/model/json.hpp>

namespace btrfsbackup {

inline constexpr const char* trusted_hook_directory = "/etc/btrfs-backup/hooks.d";
inline constexpr int current_profile_schema_version = 2;

struct ProfileTarget {
    std::string device;
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string partition_uuid;
    std::string serial;
    std::string mapper_name;
    std::string mount_point;
    std::string mount_unit;
};

struct ProfilePaths {
    std::string remote_root;
    std::string incoming_root;
};

struct ProfileSettings {
    bool daily_limit = true;
    bool incremental_required = true;
    bool keep_failed_local_snapshot = false;
    bool auto_eject = true;
    long long remote_retention = 30;
    long long local_retention = 30;
    long long minimum_target_free_bytes = 0;
    long long minimum_local_free_bytes = 0;
};

struct ProfileHookCommand {
    std::string program;
    std::vector<std::string> arguments;
    long long timeout_seconds;
};

struct ProfileHooks {
    std::vector<ProfileHookCommand> before_snapshot;
    std::vector<ProfileHookCommand> after_snapshot;
};

struct ProfileSource {
    std::string id;
    std::string name;
    bool enabled = true;
    std::string subvolume;
    std::string local_snapshot_dir;
    std::string remote_subdir;
    long long remote_retention = 30;
    long long local_retention = 30;
};

struct Profile {
    int schema_version = current_profile_schema_version;
    std::string id;
    std::string name;
    bool enabled = true;
    ProfileTarget target;
    ProfilePaths paths;
    ProfileSettings settings;
    ProfileHooks hooks;
    std::vector<ProfileSource> sources;
};

std::string identifier(const Json& value, const std::string& name);
std::string env_get(const std::map<std::string, std::string>& env, const std::string& name, const std::string& default_value = "");
std::string env_required(const std::map<std::string, std::string>& env, const std::string& name);
bool env_bool(const std::map<std::string, std::string>& env, const std::string& name, bool default_value);
long long env_int(const std::map<std::string, std::string>& env, const std::string& name, long long default_value);

Json normalize_profile(const Json& raw);
Profile profile_from_json(const Json& raw);
Json profile_to_json(const Profile& profile);

} // namespace btrfsbackup
