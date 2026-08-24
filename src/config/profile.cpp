#include <config/profile.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <config/errors.hpp>
#include <config/identifiers.hpp>
#include <config/validation.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

const std::regex uuid_re{"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"};
const std::regex serial_re{"^(|[A-Za-z0-9][A-Za-z0-9._:+-]{0,255})$"};
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

Json array_or_empty(const Json& root, const std::string& key, const std::string& name) {
    if (!root.contains(key) || root.at(key).is_null()) {
        return Json::array();
    }
    if (!root.at(key).is_array()) {
        throw ValidationError(name + " must be an array");
    }
    return root.at(key);
}

Json required_object(const Json& root, const std::string& key, const std::string& name) {
    if (!root.contains(key) || !root.at(key).is_object()) {
        throw ValidationError(name + " must be an object");
    }
    return root.at(key);
}

void reject_unknown_properties(const Json& object, const std::set<std::string>& allowed, const std::string& name) {
    if (!object.is_object()) {
        throw ValidationError(name + " must be an object");
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (allowed.count(it.key()) == 0) {
            throw ValidationError(name + "." + it.key() + " is not supported");
        }
    }
}

const Json& required_value(const Json& root, const std::string& key, const std::string& name) {
    if (!root.contains(key) || root.at(key).is_null()) {
        throw ValidationError(name + " is required");
    }
    return root.at(key);
}

Json normalize_hook_commands(const Json& hooks, const std::string& key, const std::string& name) {
    Json commands = array_or_empty(hooks, key, name);
    if (commands.size() > 64) {
        throw ValidationError(name + " supports at most 64 hooks");
    }

    Json normalized = Json::array();
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const Json& item = commands.at(index);
        const std::string item_name = name + "[" + std::to_string(index) + "]";
        if (!item.is_object()) {
            throw ValidationError(item_name + " must be an object");
        }
        reject_unknown_properties(item, {"type", "program", "arguments", "timeoutSeconds"}, item_name);
        std::string type = text(required_value(item, "type", item_name + ".type"), item_name + ".type", false, 32);
        if (type != "program") {
            throw ValidationError(item_name + ".type must be program");
        }

        Json arguments = array_or_empty(item, "arguments", item_name + ".arguments");
        if (arguments.size() > 128) {
            throw ValidationError(item_name + ".arguments supports at most 128 arguments");
        }
        Json normalized_arguments = Json::array();
        for (std::size_t argument_index = 0; argument_index < arguments.size(); ++argument_index) {
            normalized_arguments.push_back(text(
                arguments.at(argument_index),
                item_name + ".arguments[" + std::to_string(argument_index) + "]",
                true,
                4096
            ));
        }

        (void)required_value(item, "timeoutSeconds", item_name + ".timeoutSeconds");
        const long long timeout_seconds = integer_value(item, "timeoutSeconds", item_name + ".timeoutSeconds", 0, 86400);
        if (timeout_seconds == 0) {
            throw ValidationError(item_name + ".timeoutSeconds is outside the supported range");
        }

        std::string program = absolute_path(
            required_value(item, "program", item_name + ".program"),
            item_name + ".program"
        );
        fs::path normalized_program = fs::path(program).lexically_normal();
        if (normalized_program.parent_path() != fs::path(trusted_hook_directory)
            || normalized_program.filename().empty()
            || normalized_program.filename() == "."
            || normalized_program.filename() == "..") {
            throw ValidationError(
                item_name + ".program must be a direct child of " + trusted_hook_directory
            );
        }

        normalized.push_back({
            {"type", type},
            {"program", normalized_program.string()},
            {"arguments", normalized_arguments},
            {"timeoutSeconds", timeout_seconds}
        });
    }
    return normalized;
}

bool systemd_unit_plain_char(unsigned char c) {
    return std::isalnum(c) || c == ':' || c == '_' || c == '.';
}

std::string systemd_hex_escape(unsigned char c) {
    const char* digits = "0123456789abcdef";
    std::string result = "\\x";
    result.push_back(digits[(c >> 4) & 0x0f]);
    result.push_back(digits[c & 0x0f]);
    return result;
}

std::string systemd_path_unit_stem(const std::string& mount_point) {
    fs::path normalized = fs::path(mount_point).lexically_normal();
    std::string path = normalized.string();
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    if (path == "/") {
        return "-";
    }
    if (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }

    std::string escaped;
    bool previous_slash = false;
    for (unsigned char c : path) {
        if (c == '/') {
            if (!escaped.empty() && !previous_slash) {
                escaped.push_back('-');
            }
            previous_slash = true;
            continue;
        }
        previous_slash = false;
        if (systemd_unit_plain_char(c)) {
            escaped.push_back(static_cast<char>(c));
        } else {
            escaped += systemd_hex_escape(c);
        }
    }
    return escaped.empty() ? "-" : escaped;
}

std::string systemd_mount_unit(const std::string& mount_point) {
    return systemd_path_unit_stem(mount_point) + ".mount";
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

Json normalize_profile(const Json& raw, const fs::path& target_mount_root) {
    if (!raw.is_object()) {
        throw ValidationError("profile must be an object");
    }
    reject_unknown_properties(
        raw,
        {"schemaVersion", "profileId", "name", "enabled", "target", "paths", "settings", "hooks", "sources"},
        "profile"
    );
    if (!raw.contains("schemaVersion") || !raw.at("schemaVersion").is_number_integer()) {
        throw ValidationError("schemaVersion must be an integer");
    }
    int input_schema_version = raw.at("schemaVersion").get<int>();
    if (input_schema_version < 1 || input_schema_version > current_profile_schema_version) {
        throw ValidationError("schemaVersion must be 1, 2, or 3");
    }
    std::string profile_id = identifier(raw.at("profileId"), "profileId");
    std::string profile_name = text(raw.value("name", profile_id), "name", false, 160);
    bool enabled = boolean_value(raw, "enabled", "enabled", true);

    Json target = required_object(raw, "target", "target");
    reject_unknown_properties(
        target,
        {"device", "luksUuid", "btrfsUuid", "partitionUuid", "serial", "mapperName", "mountPoint", "mountUnit"},
        "target"
    );
    std::string device = absolute_path(target.at("device"), "target.device");
    if (!(device == "/dev" || starts_with(device, "/dev/"))) {
        throw ValidationError("target.device must point inside /dev");
    }
    std::string luks_uuid = uuid_value(target.at("luksUuid"), "target.luksUuid");
    std::string btrfs_uuid = uuid_value(required_value(target, "btrfsUuid", "target.btrfsUuid"), "target.btrfsUuid");
    std::string partition_uuid = uuid_value(target.value("partitionUuid", ""), "target.partitionUuid", true);
    std::string serial = text(target.value("serial", ""), "target.serial", true, 256);
    if (!std::regex_match(serial, serial_re)) {
        throw ValidationError("target.serial contains unsupported characters");
    }
    std::string mapper_name = identifier(target.at("mapperName"), "target.mapperName");
    fs::path normalized_mount_root = normalized_absolute_path(target_mount_root, "TARGET_MOUNT_ROOT");
    std::string mount_point = (normalized_mount_root / profile_id).string();
    if (input_schema_version == current_profile_schema_version && target.contains("mountPoint")) {
        throw ValidationError("target.mountPoint is application-controlled and cannot be changed");
    }
    if (target.contains("mountPoint") && absolute_path(target.at("mountPoint"), "target.mountPoint") != mount_point) {
        throw ValidationError("legacy target.mountPoint does not match TARGET_MOUNT_ROOT/profileId");
    }
    std::string mount_unit = systemd_mount_unit(mount_point);
    if (input_schema_version == current_profile_schema_version && target.contains("mountUnit")) {
        throw ValidationError("target.mountUnit is application-controlled and cannot be changed");
    }
    if (target.contains("mountUnit") && !target.at("mountUnit").is_null() && target.at("mountUnit") != "") {
        std::string configured_mount_unit = text(target.at("mountUnit"), "target.mountUnit", false, 256);
        if (configured_mount_unit != mount_unit) {
            throw ValidationError("target.mountUnit does not match target.mountPoint");
        }
    }

    Json paths = object_or_empty(raw, "paths", "paths");
    if (input_schema_version == 1) {
        reject_unknown_properties(
            paths,
            {"sourcesDir", "remoteRoot", "incomingRoot", "stateDir", "statusRoot", "historyRoot"},
            "paths"
        );
        const std::vector<std::pair<std::string, std::string>> legacy_system_paths{
            {"sourcesDir", "/etc/btrfs-backup/profiles/" + profile_id + "/sources.d"},
            {"stateDir", "/var/lib/btrfs-backup"},
            {"statusRoot", "/run/btrfs-backup/profiles"},
            {"historyRoot", "/var/lib/btrfs-backup/history"},
        };
        for (const auto& [key, fixed_value] : legacy_system_paths) {
            if (paths.contains(key) && absolute_path(paths.at(key), "paths." + key) != fixed_value) {
                throw ValidationError("paths." + key + " is application-controlled and cannot be changed");
            }
        }
    } else {
        reject_unknown_properties(paths, {"remoteRoot", "incomingRoot"}, "paths");
    }
    std::string remote_root = absolute_path(paths.value("remoteRoot", mount_point + "/snapshots"), "paths.remoteRoot");
    std::string incoming_root = absolute_path(paths.value("incomingRoot", mount_point + "/.incoming"), "paths.incomingRoot");
    if (remote_root == incoming_root || starts_with(remote_root, incoming_root + "/") || starts_with(incoming_root, remote_root + "/")) {
        throw ValidationError("paths.remoteRoot and paths.incomingRoot must be separate non-nested paths");
    }

    Json settings = object_or_empty(raw, "settings", "settings");
    Json hooks = object_or_empty(raw, "hooks", "hooks");
    reject_unknown_properties(
        settings,
        {
            "dailyLimit",
            "incrementalRequired",
            "keepFailedLocalSnapshot",
            "autoEject",
            "remoteRetention",
            "localRetention",
            "minimumTargetFreeBytes",
            "minimumLocalFreeBytes"
        },
        "settings"
    );
    reject_unknown_properties(
        hooks,
        {"beforeSnapshot", "afterSnapshot"},
        "hooks"
    );
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
        reject_unknown_properties(
            item,
            {"id", "name", "enabled", "subvolume", "localSnapshotDir", "remoteSubdir", "remoteRetention", "localRetention"},
            "sources[" + std::to_string(index) + "]"
        );
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
        {"schemaVersion", current_profile_schema_version},
        {"profileId", profile_id},
        {"name", profile_name},
        {"enabled", enabled},
        {"target", {
            {"device", device},
            {"luksUuid", luks_uuid},
            {"btrfsUuid", btrfs_uuid},
            {"partitionUuid", partition_uuid},
            {"serial", serial},
            {"mapperName", mapper_name}
        }},
        {"paths", {
            {"remoteRoot", remote_root},
            {"incomingRoot", incoming_root}
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
        {"hooks", {
            {"beforeSnapshot", normalize_hook_commands(hooks, "beforeSnapshot", "hooks.beforeSnapshot")},
            {"afterSnapshot", normalize_hook_commands(hooks, "afterSnapshot", "hooks.afterSnapshot")}
        }},
        {"sources", sources}
    };
}

Profile profile_from_json(const Json& raw, const fs::path& target_mount_root) {
    Json normalized = normalize_profile(raw, target_mount_root);
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
    profile.target.mount_point = (normalized_absolute_path(target_mount_root, "TARGET_MOUNT_ROOT") / profile.id).string();
    profile.target.mount_unit = systemd_mount_unit(profile.target.mount_point);

    const Json& paths = normalized.at("paths");
    profile.paths.remote_root = paths.at("remoteRoot").get<std::string>();
    profile.paths.incoming_root = paths.at("incomingRoot").get<std::string>();

    const Json& settings = normalized.at("settings");
    profile.settings.daily_limit = settings.at("dailyLimit").get<bool>();
    profile.settings.incremental_required = settings.at("incrementalRequired").get<bool>();
    profile.settings.keep_failed_local_snapshot = settings.at("keepFailedLocalSnapshot").get<bool>();
    profile.settings.auto_eject = settings.at("autoEject").get<bool>();
    profile.settings.remote_retention = settings.at("remoteRetention").get<long long>();
    profile.settings.local_retention = settings.at("localRetention").get<long long>();
    profile.settings.minimum_target_free_bytes = settings.at("minimumTargetFreeBytes").get<long long>();
    profile.settings.minimum_local_free_bytes = settings.at("minimumLocalFreeBytes").get<long long>();

    const Json& hooks = normalized.at("hooks");
    for (const Json& item : hooks.at("beforeSnapshot")) {
        profile.hooks.before_snapshot.push_back({
            .program = item.at("program").get<std::string>(),
            .arguments = item.at("arguments").get<std::vector<std::string>>(),
            .timeout_seconds = item.at("timeoutSeconds").get<long long>(),
        });
    }
    for (const Json& item : hooks.at("afterSnapshot")) {
        profile.hooks.after_snapshot.push_back({
            .program = item.at("program").get<std::string>(),
            .arguments = item.at("arguments").get<std::vector<std::string>>(),
            .timeout_seconds = item.at("timeoutSeconds").get<long long>(),
        });
    }

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

    Json target = {
        {"device", profile.target.device},
        {"luksUuid", profile.target.luks_uuid},
        {"btrfsUuid", profile.target.btrfs_uuid},
        {"partitionUuid", profile.target.partition_uuid},
        {"serial", profile.target.serial},
        {"mapperName", profile.target.mapper_name}
    };

    auto hooks_to_json = [](const std::vector<ProfileHookCommand>& hooks) {
        Json result = Json::array();
        for (const ProfileHookCommand& hook : hooks) {
            result.push_back({
                {"type", "program"},
                {"program", hook.program},
                {"arguments", hook.arguments},
                {"timeoutSeconds", hook.timeout_seconds}
            });
        }
        return result;
    };

    return {
        {"schemaVersion", current_profile_schema_version},
        {"profileId", profile.id},
        {"name", profile.name},
        {"enabled", profile.enabled},
        {"target", target},
        {"paths", {
            {"remoteRoot", profile.paths.remote_root},
            {"incomingRoot", profile.paths.incoming_root}
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
        {"hooks", {
            {"beforeSnapshot", hooks_to_json(profile.hooks.before_snapshot)},
            {"afterSnapshot", hooks_to_json(profile.hooks.after_snapshot)}
        }},
        {"sources", sources}
    };
}

} // namespace btrfsbackup
