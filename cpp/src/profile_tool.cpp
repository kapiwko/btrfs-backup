#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <btrfsbackup/profile_tool.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/process.hpp>

namespace fs = std::filesystem;
using json = btrfsbackup::Json;
using btrfsbackup::ValidationError;
using btrfsbackup::atomic_write;
using btrfsbackup::dump_json;
using btrfsbackup::load_json_file;
using btrfsbackup::run_capture;

namespace {

constexpr int schema_version = 1;
const std::regex profile_re{"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"};
const std::regex uuid_re{"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"};
const std::regex serial_re{"^(|[A-Za-z0-9][A-Za-z0-9._:+-]{0,255})$"};
const std::set<std::string> forbidden_mount_points{"/", "/boot", "/dev", "/etc", "/home", "/proc", "/root", "/run", "/sys", "/usr", "/var"};

json normalize_profile(const json& raw);

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backup-profile: " << message << '\n';
    std::exit(code);
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string text(const json& value, const std::string& name, bool allow_empty = false, std::size_t maximum = 512) {
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

std::string identifier(const json& value, const std::string& name) {
    std::string result = text(value, name, false, 64);
    if (!std::regex_match(result, profile_re)) {
        throw ValidationError(name + " contains unsupported characters");
    }
    return result;
}

std::string normalized_path(const std::string& value) {
    return fs::path(value).lexically_normal().string();
}

std::string absolute_path(const json& value, const std::string& name) {
    std::string result = text(value, name, false, 4096);
    if (!starts_with(result, "/")) {
        throw ValidationError(name + " must be an absolute path");
    }
    result = normalized_path(result);
    if (!starts_with(result, "/")) {
        throw ValidationError(name + " is invalid");
    }
    return result;
}

std::string relative_path(const json& value, const std::string& name) {
    std::string result = text(value, name, false, 4096);
    fs::path path(result);
    if (path.is_absolute()) {
        throw ValidationError(name + " must be a safe relative path");
    }
    for (const auto& part : path) {
        std::string item = part.string();
        if (item.empty() || item == "." || item == "..") {
            throw ValidationError(name + " must be a safe relative path");
        }
    }
    return path.lexically_normal().string();
}

std::string uuid_value(const json& value, const std::string& name, bool allow_empty = false) {
    std::string result = text(value, name, allow_empty, 64);
    if (result.empty() && allow_empty) {
        return "";
    }
    if (!std::regex_match(result, uuid_re)) {
        throw ValidationError(name + " is not a canonical UUID");
    }
    return lower(result);
}

bool boolean_value(const json& object, const std::string& key, const std::string& name, bool default_value) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    if (!object.at(key).is_boolean()) {
        throw ValidationError(name + " must be true or false");
    }
    return object.at(key).get<bool>();
}

long long integer_value(const json& object, const std::string& key, const std::string& name, long long default_value, long long maximum = 1000000000000000LL) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    if (!object.at(key).is_number_integer()) {
        throw ValidationError(name + " must be an integer");
    }
    long long result = object.at(key).get<long long>();
    if (result < 0 || result > maximum) {
        throw ValidationError(name + " is outside the supported range");
    }
    return result;
}

json object_or_empty(const json& root, const std::string& key, const std::string& name) {
    if (!root.contains(key) || root.at(key).is_null()) {
        return json::object();
    }
    if (!root.at(key).is_object()) {
        throw ValidationError(name + " must be an object");
    }
    return root.at(key);
}

json required_object(const json& root, const std::string& key, const std::string& name) {
    if (!root.contains(key) || !root.at(key).is_object()) {
        throw ValidationError(name + " must be an object");
    }
    return root.at(key);
}

std::string shell_quote(const std::string& value) {
    if (value.empty()) {
        return "''";
    }
    if (value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_@%+=:,./-") == std::string::npos) {
        return value;
    }
    std::string result = "'";
    for (char ch : value) {
        if (ch == '\'') {
            result += "'\"'\"'";
        } else {
            result += ch;
        }
    }
    result += "'";
    return result;
}

std::string assignment(const std::string& name, const json& value) {
    if (value.is_boolean()) {
        return name + "=" + std::string(value.get<bool>() ? "true" : "false") + "\n";
    }
    if (value.is_number_integer()) {
        return name + "=" + std::to_string(value.get<long long>()) + "\n";
    }
    return name + "=" + shell_quote(value.get<std::string>()) + "\n";
}

std::string systemd_mount_unit(const std::string& mount_point) {
    return run_capture({"systemd-escape", "-p", "--suffix=mount", mount_point});
}

std::map<std::string, std::string> read_shell_environment(const fs::path& path) {
    std::string script = "set -a; source \"$1\"; /usr/bin/env -0";
    std::string output = run_capture({"bash", "-c", script, "bash", path.string()});
    std::map<std::string, std::string> env;
    std::size_t start = 0;
    while (start < output.size()) {
        std::size_t end = output.find('\0', start);
        if (end == std::string::npos) {
            end = output.size();
        }
        std::string item = output.substr(start, end - start);
        std::size_t eq = item.find('=');
        if (eq != std::string::npos) {
            env[item.substr(0, eq)] = item.substr(eq + 1);
        }
        start = end + 1;
    }
    return env;
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

std::string env_get(const std::map<std::string, std::string>& env, const std::string& name, const std::string& default_value = "") {
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

bool parse_env_bool(const std::map<std::string, std::string>& env, const std::string& name) {
    return env_bool(env, name, false);
}

json profile_from_environment_sources(const fs::path& sources_table) {
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
    json sources = json::array();
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

    return normalize_profile({
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
            {"dailyLimit", parse_env_bool(env, "DAILY_LIMIT")},
            {"incrementalRequired", parse_env_bool(env, "INCREMENTAL_REQUIRED")},
            {"keepFailedLocalSnapshot", parse_env_bool(env, "KEEP_FAILED_LOCAL_SNAPSHOT")},
            {"autoEject", parse_env_bool(env, "AUTO_EJECT")},
            {"remoteRetention", env_int(env, "RETENTION_COUNT", 30)},
            {"localRetention", env_int(env, "LOCAL_RETENTION_COUNT", env_int(env, "RETENTION_COUNT", 30))},
            {"minimumTargetFreeBytes", env_int(env, "MIN_TARGET_FREE_BYTES", 5LL * 1024 * 1024 * 1024)},
            {"minimumLocalFreeBytes", env_int(env, "MIN_LOCAL_FREE_BYTES", 1024LL * 1024 * 1024)}
        }},
        {"notifications", {
            {"enabled", parse_env_bool(env, "NOTIFY_ENABLE")},
            {"user", env_get(env, "NOTIFY_USER", "")},
            {"method", env_get(env, "NOTIFY_METHOD", "auto")}
        }},
        {"sources", sources}
    });
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

fs::path profile_json_path(const fs::path& etc_root, const std::string& profile_id) {
    return etc_root / "profiles" / profile_id / "profile.json";
}

fs::path profile_env_path(const fs::path& etc_root, const std::string& profile_id) {
    return etc_root / "profiles.d" / (profile_id + ".env");
}

json normalize_profile(const json& raw) {
    if (!raw.is_object()) {
        throw ValidationError("profile must be an object");
    }
    if (!raw.contains("schemaVersion") || raw.at("schemaVersion") != schema_version) {
        throw ValidationError("schemaVersion must be 1");
    }
    std::string profile_id = identifier(raw.at("profileId"), "profileId");
    std::string profile_name = text(raw.value("name", profile_id), "name", false, 160);
    bool enabled = boolean_value(raw, "enabled", "enabled", true);

    json target = required_object(raw, "target", "target");
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

    json paths = object_or_empty(raw, "paths", "paths");
    std::string sources_dir = absolute_path(paths.value("sourcesDir", "/etc/btrfs-backup/profiles/" + profile_id + "/sources.d"), "paths.sourcesDir");
    std::string remote_root = absolute_path(paths.value("remoteRoot", mount_point + "/snapshots"), "paths.remoteRoot");
    std::string incoming_root = absolute_path(paths.value("incomingRoot", mount_point + "/.incoming"), "paths.incomingRoot");
    std::string state_dir = absolute_path(paths.value("stateDir", "/var/lib/btrfs-backup"), "paths.stateDir");
    std::string status_root = absolute_path(paths.value("statusRoot", "/run/btrfs-backup/profiles"), "paths.statusRoot");
    std::string history_root = absolute_path(paths.value("historyRoot", "/var/lib/btrfs-backup/history"), "paths.historyRoot");
    if (remote_root == incoming_root || starts_with(remote_root, incoming_root + "/") || starts_with(incoming_root, remote_root + "/")) {
        throw ValidationError("paths.remoteRoot and paths.incomingRoot must be separate non-nested paths");
    }

    json settings = object_or_empty(raw, "settings", "settings");
    json notifications = object_or_empty(raw, "notifications", "notifications");
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
    json sources = json::array();
    bool any_enabled = false;
    for (std::size_t index = 0; index < raw.at("sources").size(); ++index) {
        const json& item = raw.at("sources").at(index);
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

json load_profile_from_runtime(const fs::path& etc_root, const std::string& profile_id) {
    std::map<std::string, std::string> env;
    fs::path env_path = profile_env_path(etc_root, profile_id);
    if (fs::exists(env_path)) {
        env = read_shell_environment(env_path);
    } else if (profile_id == "default" && fs::exists(etc_root / "backup.env")) {
        env = read_shell_environment(etc_root / "backup.env");
    } else {
        throw ValidationError("profile JSON or runtime env not found for profile: " + profile_id);
    }
    std::string resolved = env_get(env, "PROFILE_ID", profile_id);
    if (resolved != profile_id) {
        throw ValidationError("requested profile " + profile_id + " but runtime env declares PROFILE_ID=" + resolved);
    }
    std::string sources_dir = env_get(env, "SOURCES_DIR", "/etc/btrfs-backup/sources.d");
    fs::path source_root = map_etc_path(sources_dir, etc_root);
    std::vector<fs::path> source_files;
    for (const auto& entry : fs::directory_iterator(source_root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".conf") {
            source_files.push_back(entry.path());
        }
    }
    std::sort(source_files.begin(), source_files.end());
    if (source_files.empty()) {
        throw ValidationError("no source definitions found in " + source_root.string());
    }
    long long remote_retention = env_int(env, "RETENTION_COUNT", 30);
    long long local_retention = env_int(env, "LOCAL_RETENTION_COUNT", remote_retention);
    json sources = json::array();
    for (const auto& source_file : source_files) {
        auto source_env = read_shell_environment(source_file);
        if (!env_bool(source_env, "ENABLED", true)) {
            continue;
        }
        std::string source_id = env_required(source_env, "SOURCE_NAME");
        sources.push_back({
            {"id", source_id},
            {"name", env_get(source_env, "SOURCE_DISPLAY_NAME", source_id)},
            {"enabled", true},
            {"subvolume", env_required(source_env, "SOURCE_SUBVOLUME")},
            {"localSnapshotDir", env_required(source_env, "LOCAL_SNAPSHOT_DIR")},
            {"remoteSubdir", env_get(source_env, "REMOTE_SUBDIR", source_id)},
            {"remoteRetention", env_int(source_env, "SOURCE_RETENTION_COUNT", remote_retention)},
            {"localRetention", env_int(source_env, "SOURCE_LOCAL_RETENTION_COUNT", local_retention)}
        });
    }
    std::string mount = env_required(env, "BACKUP_MOUNTPOINT");
    return normalize_profile({
        {"schemaVersion", schema_version},
        {"profileId", profile_id},
        {"name", env_get(env, "PROFILE_NAME", profile_id)},
        {"enabled", true},
        {"target", {
            {"device", env_required(env, "BACKUP_DEVICE")},
            {"luksUuid", env_required(env, "BACKUP_LUKS_UUID")},
            {"btrfsUuid", env_get(env, "BACKUP_BTRFS_UUID", "")},
            {"partitionUuid", ""},
            {"serial", ""},
            {"mapperName", env_required(env, "BACKUP_MAPPER_NAME")},
            {"mountPoint", mount}
        }},
        {"paths", {
            {"sourcesDir", sources_dir},
            {"remoteRoot", env_get(env, "REMOTE_ROOT", mount + "/snapshots")},
            {"incomingRoot", env_get(env, "INCOMING_ROOT", mount + "/.incoming")},
            {"stateDir", env_get(env, "STATE_DIR", "/var/lib/btrfs-backup")},
            {"statusRoot", env_get(env, "STATUS_ROOT", "/run/btrfs-backup/profiles")},
            {"historyRoot", env_get(env, "HISTORY_ROOT", env_get(env, "STATE_DIR", "/var/lib/btrfs-backup") + "/history")}
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

json load_profile_by_id(const fs::path& etc_root, const std::string& profile_id) {
    if (!std::regex_match(profile_id, profile_re)) {
        throw ValidationError("profile contains unsupported characters");
    }
    fs::path canonical = profile_json_path(etc_root, profile_id);
    if (fs::exists(canonical)) {
        return normalize_profile(load_json_file(canonical));
    }
    return load_profile_from_runtime(etc_root, profile_id);
}

std::string render_profile_env(const json& profile) {
    const auto& target = profile.at("target");
    const auto& paths = profile.at("paths");
    const auto& settings = profile.at("settings");
    const auto& notifications = profile.at("notifications");
    std::vector<std::pair<std::string, json>> values{
        {"PROFILE_ID", profile.at("profileId")},
        {"PROFILE_NAME", profile.at("name")},
        {"BACKUP_MAPPER_NAME", target.at("mapperName")},
        {"BACKUP_DEVICE", target.at("device")},
        {"BACKUP_LUKS_UUID", target.at("luksUuid")},
        {"BACKUP_BTRFS_UUID", target.at("btrfsUuid")},
        {"BACKUP_MOUNTPOINT", target.at("mountPoint")},
        {"BACKUP_MOUNT_UNIT", target.at("mountUnit")},
        {"BACKUP_SERVICE_NAME", "btrfs-backup@" + profile.at("profileId").get<std::string>() + ".service"},
        {"SOURCES_DIR", paths.at("sourcesDir")},
        {"REMOTE_ROOT", paths.at("remoteRoot")},
        {"INCOMING_ROOT", paths.at("incomingRoot")},
        {"RETENTION_COUNT", settings.at("remoteRetention")},
        {"LOCAL_RETENTION_COUNT", settings.at("localRetention")},
        {"DAILY_LIMIT", settings.at("dailyLimit")},
        {"INCREMENTAL_REQUIRED", settings.at("incrementalRequired")},
        {"KEEP_FAILED_LOCAL_SNAPSHOT", settings.at("keepFailedLocalSnapshot")},
        {"AUTO_EJECT", settings.at("autoEject")},
        {"MIN_TARGET_FREE_BYTES", settings.at("minimumTargetFreeBytes")},
        {"MIN_LOCAL_FREE_BYTES", settings.at("minimumLocalFreeBytes")},
        {"LOCK_FILE", "/run/btrfs-backup/" + profile.at("profileId").get<std::string>() + ".lock"},
        {"STATE_DIR", paths.at("stateDir")},
        {"STATUS_ROOT", paths.at("statusRoot")},
        {"HISTORY_ROOT", paths.at("historyRoot")},
        {"EJECT_SCRIPT_PATH", "/usr/lib/btrfs-backup/btrfs-backup-eject.sh"},
        {"NOTIFY_ENABLE", notifications.at("enabled")},
        {"NOTIFY_USER", notifications.at("user")},
        {"NOTIFY_METHOD", notifications.at("method")}
    };
    std::string out = "# Generated by btrfs-backup-profile. Manual changes may be overwritten.\n";
    for (const auto& [key, value] : values) {
        out += assignment(key, value);
    }
    return out;
}

std::string render_source(const json& source) {
    std::vector<std::pair<std::string, json>> values{
        {"ENABLED", source.at("enabled")},
        {"SOURCE_NAME", source.at("id")},
        {"SOURCE_DISPLAY_NAME", source.at("name")},
        {"SOURCE_SUBVOLUME", source.at("subvolume")},
        {"LOCAL_SNAPSHOT_DIR", source.at("localSnapshotDir")},
        {"REMOTE_SUBDIR", source.at("remoteSubdir")},
        {"SOURCE_RETENTION_COUNT", source.at("remoteRetention")},
        {"SOURCE_LOCAL_RETENTION_COUNT", source.at("localRetention")}
    };
    std::string out = "# Generated by btrfs-backup-profile.\n";
    for (const auto& [key, value] : values) {
        out += assignment(key, value);
    }
    return out;
}

std::string render_udev(const json& profile) {
    if (!profile.at("enabled").get<bool>()) {
        return "# Profile disabled; no automatic activation rule.\n";
    }
    const auto& target = profile.at("target");
    std::vector<std::string> matches{
        "ACTION==\"add\"",
        "SUBSYSTEM==\"block\"",
        "ENV{ID_FS_TYPE}==\"crypto_LUKS\"",
        "ENV{ID_FS_UUID}==\"" + target.at("luksUuid").get<std::string>() + "\""
    };
    if (!target.at("partitionUuid").get<std::string>().empty()) {
        matches.push_back("ENV{ID_PART_ENTRY_UUID}==\"" + target.at("partitionUuid").get<std::string>() + "\"");
    }
    if (!target.at("serial").get<std::string>().empty()) {
        matches.push_back("ENV{ID_SERIAL_SHORT}==\"" + target.at("serial").get<std::string>() + "\"");
    }
    matches.push_back("TAG+=\"systemd\"");
    matches.push_back("ENV{SYSTEMD_WANTS}+=\"btrfs-backup@" + profile.at("profileId").get<std::string>() + ".service\"");
    std::ostringstream out;
    out << "# Generated by btrfs-backup-profile.\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << matches[i];
    }
    out << "\n";
    return out.str();
}

std::string iso_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S+00:00", &tm);
    return buffer;
}

void render_tree(const json& profile, const fs::path& output_dir) {
    std::string profile_id = profile.at("profileId").get<std::string>();
    fs::path root = output_dir / "etc" / "btrfs-backup";
    atomic_write(root / "profiles.d" / (profile_id + ".env"), render_profile_env(profile), 0600);
    int index = 1;
    for (const auto& source : profile.at("sources")) {
        std::ostringstream name;
        name << std::setw(3) << std::setfill('0') << index * 10 << "-" << source.at("id").get<std::string>() << ".conf";
        atomic_write(root / "profiles" / profile_id / "sources.d" / name.str(), render_source(source), 0600);
        ++index;
    }
    atomic_write(output_dir / "etc" / "udev" / "rules.d" / ("99-btrfs-backup-" + profile_id + ".rules"), render_udev(profile), 0644);
    json public_profile = profile;
    public_profile["generatedAt"] = iso_now();
    atomic_write(output_dir / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / (profile_id + ".json"), dump_json(public_profile), 0644);
}

void save_tree(const json& profile, const fs::path& etc_root, const fs::path& udev_root, const fs::path& public_root) {
    std::string profile_id = profile.at("profileId").get<std::string>();
    fs::path source_root = map_etc_path(profile.at("paths").at("sourcesDir").get<std::string>(), etc_root);
    atomic_write(etc_root / "profiles.d" / (profile_id + ".env"), render_profile_env(profile), 0600);
    if (fs::exists(source_root)) {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", &tm);
        fs::rename(source_root, source_root.parent_path() / (source_root.filename().string() + ".backup-" + stamp));
    }
    int index = 1;
    for (const auto& source : profile.at("sources")) {
        std::ostringstream name;
        name << std::setw(3) << std::setfill('0') << index * 10 << "-" << source.at("id").get<std::string>() << ".conf";
        atomic_write(source_root / name.str(), render_source(source), 0600);
        ++index;
    }
    atomic_write(udev_root / ("99-btrfs-backup-" + profile_id + ".rules"), render_udev(profile), 0644);
    json public_profile = profile;
    public_profile["generatedAt"] = iso_now();
    atomic_write(public_root / (profile_id + ".json"), dump_json(public_profile), 0644);
}

std::string arg_value(int& index, int argc, char** argv, const std::string& option) {
    if (index + 1 >= argc) {
        fail(option + " requires a value");
    }
    return argv[++index];
}

void usage() {
    std::cout << "Usage: btrfs-backup-profile [--etc-root PATH] [--udev-root PATH] [--public-root PATH] COMMAND\n"
              << "\nCommands:\n"
              << "  compose --sources-table PATH --output PATH\n"
              << "  validate --file PATH\n"
              << "  render --file PATH --output-dir PATH\n"
              << "  save --file PATH\n"
              << "  show [--profile ID]\n"
              << "  export [--profile ID] --output PATH\n";
}

} // namespace

namespace btrfsbackup {

int profile_tool_main(int argc, char** argv) {
    fs::path etc_root = std::getenv("BTRFS_BACKUP_ETC_ROOT") ? std::getenv("BTRFS_BACKUP_ETC_ROOT") : "/etc/btrfs-backup";
    fs::path udev_root = std::getenv("BTRFS_BACKUP_UDEV_ROOT") ? std::getenv("BTRFS_BACKUP_UDEV_ROOT") : "/etc/udev/rules.d";
    fs::path public_root = std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") ? std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") : "/var/lib/btrfs-backup/public/profiles";
    std::vector<std::string> rest;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--etc-root") {
                etc_root = arg_value(i, argc, argv, arg);
            } else if (arg == "--udev-root") {
                udev_root = arg_value(i, argc, argv, arg);
            } else if (arg == "--public-root") {
                public_root = arg_value(i, argc, argv, arg);
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                for (; i < argc; ++i) {
                    rest.emplace_back(argv[i]);
                }
                break;
            }
        }
        if (rest.empty()) {
            usage();
            return 2;
        }
        std::string command = rest[0];
        fs::path file;
        fs::path output_dir;
        fs::path sources_table;
        std::string profile_id = "default";
        for (std::size_t i = 1; i < rest.size(); ++i) {
            const std::string& arg = rest[i];
            if (arg == "--file" && i + 1 < rest.size()) {
                file = rest[++i];
            } else if (arg == "--output-dir" && i + 1 < rest.size()) {
                output_dir = rest[++i];
            } else if (arg == "--profile" && i + 1 < rest.size()) {
                profile_id = rest[++i];
            } else if (arg == "--output" && i + 1 < rest.size()) {
                output_dir = rest[++i];
            } else if (arg == "--sources-table" && i + 1 < rest.size()) {
                sources_table = rest[++i];
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                fail("unknown option: " + arg);
            }
        }

        if (command == "compose") {
            if (sources_table.empty()) fail("compose requires --sources-table");
            if (output_dir.empty()) fail("compose requires --output");
            atomic_write(output_dir, dump_json(profile_from_environment_sources(sources_table)), 0600);
        } else if (command == "validate") {
            if (file.empty()) fail("validate requires --file");
            std::cout << dump_json(normalize_profile(load_json_file(file)));
        } else if (command == "render") {
            if (file.empty()) fail("render requires --file");
            if (output_dir.empty()) fail("render requires --output-dir");
            output_dir = fs::absolute(output_dir).lexically_normal();
            if (output_dir == "/" || output_dir == "/etc" || output_dir == "/usr" || output_dir == "/var") {
                throw ValidationError("refusing unsafe output directory: " + output_dir.string());
            }
            std::error_code ec;
            fs::remove_all(output_dir, ec);
            json profile = normalize_profile(load_json_file(file));
            render_tree(profile, output_dir);
            std::cout << "Rendered profile " << profile.at("profileId").get<std::string>() << " to " << output_dir << "\n";
        } else if (command == "save") {
            if (file.empty()) fail("save requires --file");
            if (geteuid() != 0 && etc_root == "/etc/btrfs-backup") {
                fail("save to system configuration must be run as root", 1);
            }
            json profile = normalize_profile(load_json_file(file));
            save_tree(profile, etc_root, udev_root, public_root);
            std::cout << "Saved profile " << profile.at("profileId").get<std::string>() << "\n";
        } else if (command == "show") {
            std::cout << dump_json(load_profile_by_id(etc_root, profile_id));
        } else if (command == "export") {
            if (output_dir.empty()) fail("export requires --output");
            json profile = load_profile_by_id(etc_root, profile_id);
            atomic_write(output_dir, dump_json(profile), 0600);
            std::cout << "Exported profile " << profile.at("profileId").get<std::string>() << " to " << output_dir << "\n";
        } else {
            fail("unknown command: " + command);
        }
    } catch (const ValidationError& exc) {
        fail(exc.what());
    } catch (const std::exception& exc) {
        fail(exc.what());
    }
    return 0;
}

} // namespace btrfsbackup
