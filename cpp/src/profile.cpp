#include <btrfsbackup/profile.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/process.hpp>
#include <btrfsbackup/validation.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

constexpr int schema_version = 1;
const std::regex uuid_re{"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"};
const std::regex serial_re{"^(|[A-Za-z0-9][A-Za-z0-9._:+-]{0,255})$"};
const std::set<std::string> forbidden_mount_points{"/", "/boot", "/dev", "/etc", "/home", "/proc", "/root", "/run", "/sys", "/usr", "/var"};

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string text(const Json& value, const std::string& name, bool allow_empty = false, std::size_t maximum = 512) {
    if (!value.is_string()) {
        throw ValidationError(name + " must be text");
    }
    std::string result = value.get<std::string>();
    if (result.find('\0') != std::string::npos || result.find('\n') != std::string::npos || result.find('\r') != std::string::npos) {
        throw ValidationError(name + " contains a forbidden control character");
    }
    if (result.size() > maximum) {
        throw ValidationError(name + " is too long");
    }
    if (!allow_empty && result.empty()) {
        throw ValidationError(name + " must not be empty");
    }
    return result;
}

std::string absolute_path(const Json& value, const std::string& name) {
    std::string result = text(value, name, false, 4096);
    return normalized_absolute_path(result, name).string();
}

std::string relative_path(const Json& value, const std::string& name) {
    std::string result = text(value, name, false, 4096);
    return normalized_relative_path(result, name).string();
}

std::string uuid_value(const Json& value, const std::string& name, bool allow_empty = false) {
    std::string result = text(value, name, allow_empty, 64);
    if (result.empty() && allow_empty) {
        return "";
    }
    if (!std::regex_match(result, uuid_re)) {
        throw ValidationError(name + " is not a canonical UUID");
    }
    return lower(result);
}

bool boolean_value(const Json& object, const std::string& key, const std::string& name, bool default_value) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    if (!object.at(key).is_boolean()) {
        throw ValidationError(name + " must be true or false");
    }
    return object.at(key).get<bool>();
}

long long integer_value(const Json& object, const std::string& key, const std::string& name, long long default_value, long long maximum = 1000000000000000LL) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    if (!object.at(key).is_number_integer()) {
        throw ValidationError(name + " must be an integer");
    }
    long long result = object.at(key).get<long long>();
    if (result < 0) {
        throw ValidationError(name + " is outside the supported range");
    }
    return parse_uint(std::to_string(result), name, maximum);
}

Json object_or_empty(const Json& root, const std::string& key, const std::string& name) {
    if (!root.contains(key) || root.at(key).is_null()) {
        return Json::object();
    }
    if (!root.at(key).is_object()) {
        throw ValidationError(name + " must be an object");
    }
    return root.at(key);
}

Json required_object(const Json& root, const std::string& key, const std::string& name) {
    if (!root.contains(key) || !root.at(key).is_object()) {
        throw ValidationError(name + " must be an object");
    }
    return root.at(key);
}

std::string systemd_mount_unit(const std::string& mount_point) {
    return run_capture({"systemd-escape", "-p", "--suffix=mount", mount_point});
}

fs::path profile_json_path(const fs::path& etc_root, const std::string& profile_id) {
    return etc_root / "profiles" / profile_id / "profile.json";
}

} // namespace

std::string identifier(const Json& value, const std::string& name) {
    std::string result = text(value, name, false, 64);
    validate_identifier(result, name);
    return result;
}

std::string env_get(const std::map<std::string, std::string>& env, const std::string& name, const std::string& default_value) {
    auto it = env.find(name);
    return it == env.end() ? default_value : it->second;
}

std::string env_required(const std::map<std::string, std::string>& env, const std::string& name) {
    std::string value = env_get(env, name);
    if (value.empty()) {
        throw ValidationError("missing required configuration variable: " + name);
    }
    return value;
}

bool env_bool(const std::map<std::string, std::string>& env, const std::string& name, bool default_value) {
    auto it = env.find(name);
    if (it == env.end() || it->second.empty()) {
        return default_value;
    }
    std::string value = lower(it->second);
    if (value == "1" || value == "yes" || value == "true" || value == "on") {
        return true;
    }
    if (value == "0" || value == "no" || value == "false" || value == "off") {
        return false;
    }
    throw ValidationError(name + " must be true or false");
}

long long env_int(const std::map<std::string, std::string>& env, const std::string& name, long long default_value) {
    auto it = env.find(name);
    if (it == env.end() || it->second.empty()) {
        return default_value;
    }
    if (!std::all_of(it->second.begin(), it->second.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw ValidationError(name + " must be an integer");
    }
    return std::stoll(it->second);
}

fs::path map_etc_path(const std::string& path, const fs::path& etc_root) {
    fs::path configured(path);
    fs::path default_root("/etc/btrfs-backup");
    auto normalized = configured.lexically_normal();
    auto root_normalized = default_root.lexically_normal();
    auto root_it = root_normalized.begin();
    auto path_it = normalized.begin();
    for (; root_it != root_normalized.end() && path_it != normalized.end(); ++root_it, ++path_it) {
        if (*root_it != *path_it) {
            return configured;
        }
    }
    if (root_it == root_normalized.end()) {
        fs::path suffix;
        for (; path_it != normalized.end(); ++path_it) {
            suffix /= *path_it;
        }
        return etc_root / suffix;
    }
    return configured;
}

Json normalize_profile(const Json& raw) {
    if (!raw.is_object()) {
        throw ValidationError("profile must be an object");
    }
    if (!raw.contains("schemaVersion") || raw.at("schemaVersion") != schema_version) {
        throw ValidationError("schemaVersion must be 1");
    }
    std::string profile_id = identifier(raw.at("profileId"), "profileId");
    std::string profile_name = text(raw.value("name", profile_id), "name", false, 160);
    bool enabled = boolean_value(raw, "enabled", "enabled", true);

    Json target = required_object(raw, "target", "target");
    std::string device = absolute_path(target.at("device"), "target.device");
    if (!(device == "/dev" || starts_with(device, "/dev/"))) {
        throw ValidationError("target.device must point inside /dev");
    }
    std::string luks_uuid = uuid_value(target.at("luksUuid"), "target.luksUuid");
    std::string btrfs_uuid = uuid_value(target.value("btrfsUuid", ""), "target.btrfsUuid", true);
    std::string partition_uuid = uuid_value(target.value("partitionUuid", ""), "target.partitionUuid", true);
    std::string serial = text(target.value("serial", ""), "target.serial", true, 256);
    if (!std::regex_match(serial, serial_re)) {
        throw ValidationError("target.serial contains unsupported characters");
    }
    std::string mapper_name = identifier(target.at("mapperName"), "target.mapperName");
    std::string mount_point = absolute_path(target.at("mountPoint"), "target.mountPoint");
    if (forbidden_mount_points.count(mount_point) > 0) {
        throw ValidationError("target.mountPoint is unsafe: " + mount_point);
    }

    Json paths = object_or_empty(raw, "paths", "paths");
    std::string sources_dir = absolute_path(paths.value("sourcesDir", "/etc/btrfs-backup/profiles/" + profile_id + "/sources.d"), "paths.sourcesDir");
    std::string remote_root = absolute_path(paths.value("remoteRoot", mount_point + "/snapshots"), "paths.remoteRoot");
    std::string incoming_root = absolute_path(paths.value("incomingRoot", mount_point + "/.incoming"), "paths.incomingRoot");
    std::string state_dir = absolute_path(paths.value("stateDir", "/var/lib/btrfs-backup"), "paths.stateDir");
    std::string status_root = absolute_path(paths.value("statusRoot", "/run/btrfs-backup/profiles"), "paths.statusRoot");
    std::string history_root = absolute_path(paths.value("historyRoot", "/var/lib/btrfs-backup/history"), "paths.historyRoot");
    if (remote_root == incoming_root || starts_with(remote_root, incoming_root + "/") || starts_with(incoming_root, remote_root + "/")) {
        throw ValidationError("paths.remoteRoot and paths.incomingRoot must be separate non-nested paths");
    }

    Json settings = object_or_empty(raw, "settings", "settings");
    Json notifications = object_or_empty(raw, "notifications", "notifications");
    std::string notify_method = text(notifications.value("method", "auto"), "notifications.method");
    if (notify_method != "auto" && notify_method != "desktop" && notify_method != "journal" && notify_method != "none") {
        throw ValidationError("notifications.method must be auto, desktop, journal, or none");
    }
    long long remote_retention = integer_value(settings, "remoteRetention", "settings.remoteRetention", 30, 100000);
    long long local_retention = integer_value(settings, "localRetention", "settings.localRetention", 30, 100000);

    if (!raw.contains("sources") || !raw.at("sources").is_array()) {
        throw ValidationError("sources must be an array");
    }
    if (raw.at("sources").empty()) {
        throw ValidationError("sources must contain at least one source");
    }
    if (raw.at("sources").size() > 128) {
        throw ValidationError("at most 128 sources are supported");
    }

    std::set<std::string> seen_ids;
    std::set<std::string> seen_local;
    std::set<std::string> seen_remote;
    Json sources = Json::array();
    bool any_enabled = false;
    for (std::size_t index = 0; index < raw.at("sources").size(); ++index) {
        const Json& item = raw.at("sources").at(index);
        if (!item.is_object()) {
            throw ValidationError("sources[" + std::to_string(index) + "] must be an object");
        }
        std::string source_id = identifier(item.at("id"), "sources[" + std::to_string(index) + "].id");
        if (!seen_ids.insert(source_id).second) {
            throw ValidationError("duplicate source id: " + source_id);
        }
        std::string local = absolute_path(item.at("localSnapshotDir"), "sources[" + std::to_string(index) + "].localSnapshotDir");
        std::string remote = relative_path(item.value("remoteSubdir", source_id), "sources[" + std::to_string(index) + "].remoteSubdir");
        if (!seen_local.insert(local).second) {
            throw ValidationError("duplicate localSnapshotDir: " + local);
        }
        if (!seen_remote.insert(remote).second) {
            throw ValidationError("duplicate remoteSubdir: " + remote);
        }
        bool source_enabled = boolean_value(item, "enabled", "sources[" + std::to_string(index) + "].enabled", true);
        any_enabled = any_enabled || source_enabled;
        sources.push_back({
            {"id", source_id},
            {"name", text(item.value("name", source_id), "sources[" + std::to_string(index) + "].name", false, 160)},
            {"enabled", source_enabled},
            {"subvolume", absolute_path(item.at("subvolume"), "sources[" + std::to_string(index) + "].subvolume")},
            {"localSnapshotDir", local},
            {"remoteSubdir", remote},
            {"remoteRetention", integer_value(item, "remoteRetention", "sources[" + std::to_string(index) + "].remoteRetention", remote_retention, 100000)},
            {"localRetention", integer_value(item, "localRetention", "sources[" + std::to_string(index) + "].localRetention", local_retention, 100000)}
        });
    }
    if (!any_enabled) {
        throw ValidationError("at least one source must be enabled");
    }

    return {
        {"schemaVersion", schema_version},
        {"profileId", profile_id},
        {"name", profile_name},
        {"enabled", enabled},
        {"target", {
            {"device", device},
            {"luksUuid", luks_uuid},
            {"btrfsUuid", btrfs_uuid},
            {"partitionUuid", partition_uuid},
            {"serial", serial},
            {"mapperName", mapper_name},
            {"mountPoint", mount_point},
            {"mountUnit", systemd_mount_unit(mount_point)}
        }},
        {"paths", {
            {"sourcesDir", sources_dir},
            {"remoteRoot", remote_root},
            {"incomingRoot", incoming_root},
            {"stateDir", state_dir},
            {"statusRoot", status_root},
            {"historyRoot", history_root}
        }},
        {"settings", {
            {"dailyLimit", boolean_value(settings, "dailyLimit", "settings.dailyLimit", true)},
            {"incrementalRequired", boolean_value(settings, "incrementalRequired", "settings.incrementalRequired", true)},
            {"keepFailedLocalSnapshot", boolean_value(settings, "keepFailedLocalSnapshot", "settings.keepFailedLocalSnapshot", false)},
            {"autoEject", boolean_value(settings, "autoEject", "settings.autoEject", true)},
            {"remoteRetention", remote_retention},
            {"localRetention", local_retention},
            {"minimumTargetFreeBytes", integer_value(settings, "minimumTargetFreeBytes", "settings.minimumTargetFreeBytes", 5LL * 1024 * 1024 * 1024)},
            {"minimumLocalFreeBytes", integer_value(settings, "minimumLocalFreeBytes", "settings.minimumLocalFreeBytes", 1024LL * 1024 * 1024)}
        }},
        {"notifications", {
            {"enabled", boolean_value(notifications, "enabled", "notifications.enabled", true)},
            {"user", text(notifications.value("user", ""), "notifications.user", true, 256)},
            {"method", notify_method}
        }},
        {"sources", sources}
    };
}

Profile profile_from_json(const Json& raw) {
    Json normalized = normalize_profile(raw);
    Profile profile;
    profile.schema_version = normalized.at("schemaVersion").get<int>();
    profile.id = normalized.at("profileId").get<std::string>();
    profile.name = normalized.at("name").get<std::string>();
    profile.enabled = normalized.at("enabled").get<bool>();

    const Json& target = normalized.at("target");
    profile.target.device = target.at("device").get<std::string>();
    profile.target.luks_uuid = target.at("luksUuid").get<std::string>();
    profile.target.btrfs_uuid = target.at("btrfsUuid").get<std::string>();
    profile.target.partition_uuid = target.at("partitionUuid").get<std::string>();
    profile.target.serial = target.at("serial").get<std::string>();
    profile.target.mapper_name = target.at("mapperName").get<std::string>();
    profile.target.mount_point = target.at("mountPoint").get<std::string>();
    profile.target.mount_unit = target.at("mountUnit").get<std::string>();

    const Json& paths = normalized.at("paths");
    profile.paths.sources_dir = paths.at("sourcesDir").get<std::string>();
    profile.paths.remote_root = paths.at("remoteRoot").get<std::string>();
    profile.paths.incoming_root = paths.at("incomingRoot").get<std::string>();
    profile.paths.state_dir = paths.at("stateDir").get<std::string>();
    profile.paths.status_root = paths.at("statusRoot").get<std::string>();
    profile.paths.history_root = paths.at("historyRoot").get<std::string>();

    const Json& settings = normalized.at("settings");
    profile.settings.daily_limit = settings.at("dailyLimit").get<bool>();
    profile.settings.incremental_required = settings.at("incrementalRequired").get<bool>();
    profile.settings.keep_failed_local_snapshot = settings.at("keepFailedLocalSnapshot").get<bool>();
    profile.settings.auto_eject = settings.at("autoEject").get<bool>();
    profile.settings.remote_retention = settings.at("remoteRetention").get<long long>();
    profile.settings.local_retention = settings.at("localRetention").get<long long>();
    profile.settings.minimum_target_free_bytes = settings.at("minimumTargetFreeBytes").get<long long>();
    profile.settings.minimum_local_free_bytes = settings.at("minimumLocalFreeBytes").get<long long>();

    const Json& notifications = normalized.at("notifications");
    profile.notifications.enabled = notifications.at("enabled").get<bool>();
    profile.notifications.user = notifications.at("user").get<std::string>();
    profile.notifications.method = notifications.at("method").get<std::string>();

    for (const Json& item : normalized.at("sources")) {
        profile.sources.push_back({
            .id = item.at("id").get<std::string>(),
            .name = item.at("name").get<std::string>(),
            .enabled = item.at("enabled").get<bool>(),
            .subvolume = item.at("subvolume").get<std::string>(),
            .local_snapshot_dir = item.at("localSnapshotDir").get<std::string>(),
            .remote_subdir = item.at("remoteSubdir").get<std::string>(),
            .remote_retention = item.at("remoteRetention").get<long long>(),
            .local_retention = item.at("localRetention").get<long long>(),
        });
    }
    return profile;
}

Json profile_to_json(const Profile& profile) {
    Json sources = Json::array();
    for (const ProfileSource& source : profile.sources) {
        sources.push_back({
            {"id", source.id},
            {"name", source.name},
            {"enabled", source.enabled},
            {"subvolume", source.subvolume},
            {"localSnapshotDir", source.local_snapshot_dir},
            {"remoteSubdir", source.remote_subdir},
            {"remoteRetention", source.remote_retention},
            {"localRetention", source.local_retention}
        });
    }

    return {
        {"schemaVersion", profile.schema_version},
        {"profileId", profile.id},
        {"name", profile.name},
        {"enabled", profile.enabled},
        {"target", {
            {"device", profile.target.device},
            {"luksUuid", profile.target.luks_uuid},
            {"btrfsUuid", profile.target.btrfs_uuid},
            {"partitionUuid", profile.target.partition_uuid},
            {"serial", profile.target.serial},
            {"mapperName", profile.target.mapper_name},
            {"mountPoint", profile.target.mount_point},
            {"mountUnit", profile.target.mount_unit}
        }},
        {"paths", {
            {"sourcesDir", profile.paths.sources_dir},
            {"remoteRoot", profile.paths.remote_root},
            {"incomingRoot", profile.paths.incoming_root},
            {"stateDir", profile.paths.state_dir},
            {"statusRoot", profile.paths.status_root},
            {"historyRoot", profile.paths.history_root}
        }},
        {"settings", {
            {"dailyLimit", profile.settings.daily_limit},
            {"incrementalRequired", profile.settings.incremental_required},
            {"keepFailedLocalSnapshot", profile.settings.keep_failed_local_snapshot},
            {"autoEject", profile.settings.auto_eject},
            {"remoteRetention", profile.settings.remote_retention},
            {"localRetention", profile.settings.local_retention},
            {"minimumTargetFreeBytes", profile.settings.minimum_target_free_bytes},
            {"minimumLocalFreeBytes", profile.settings.minimum_local_free_bytes}
        }},
        {"notifications", {
            {"enabled", profile.notifications.enabled},
            {"user", profile.notifications.user},
            {"method", profile.notifications.method}
        }},
        {"sources", sources}
    };
}

namespace {

Json load_profile_json(const fs::path& etc_root, const std::string& profile_id) {
    validate_identifier(profile_id, "profile");
    fs::path canonical = profile_json_path(etc_root, profile_id);
    if (fs::exists(canonical)) {
        return normalize_profile(load_json_file(canonical));
    }
    throw ValidationError("profile JSON not found for profile: " + profile_id);
}

} // namespace

Profile load_profile_by_id(const fs::path& etc_root, const std::string& profile_id) {
    return profile_from_json(load_profile_json(etc_root, profile_id));
}

} // namespace btrfsbackup
