// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/model/profile_document.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <core/errors.hpp>
#include <core/identifiers.hpp>
#include <config/model/json.hpp>
#include <config/model/validation.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::config {

namespace {

const std::regex uuid_re{"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"};
const std::regex serial_re{"^(|[A-Za-z0-9][A-Za-z0-9._:+-]{0,255})$"};
const std::regex configuration_generation_re{"^[0-9a-f]{32}$"};
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

std::uint64_t integer_value(
    const Json& object,
    const std::string& key,
    const std::string& name,
    std::uint64_t default_value,
    std::uint64_t maximum = 1000000000000000ULL
) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    if (!object.at(key).is_number_integer()) {
        throw ValidationError(name + " must be an integer");
    }
    if (object.at(key).is_number_unsigned()) {
        const std::uint64_t result = object.at(key).get<std::uint64_t>();
        if (result > maximum) {
            throw ValidationError(name + " is outside the supported range");
        }
        return result;
    }
    const std::int64_t result = object.at(key).get<std::int64_t>();
    if (result < 0 || static_cast<std::uint64_t>(result) > maximum) {
        throw ValidationError(name + " is outside the supported range");
    }
    return static_cast<std::uint64_t>(result);
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
        const std::uint64_t timeout_seconds = integer_value(item, "timeoutSeconds", item_name + ".timeoutSeconds", 0, 86400);
        if (timeout_seconds == 0) {
            throw ValidationError(item_name + ".timeoutSeconds is outside the supported range");
        }

        std::string program = absolute_path(
            required_value(item, "program", item_name + ".program"),
            item_name + ".program"
        );
        normalized.push_back({{"type", type}, {"program", fs::path(program).lexically_normal().string()}, {"arguments", normalized_arguments}, {"timeoutSeconds", timeout_seconds}});
    }
    return normalized;
}

} // namespace

std::string identifier(const Json& value, const std::string& name) {
    std::string result = text(value, name, false, 64);
    validate_identifier(result, name);
    return result;
}

Json normalize_profile(const Json& raw, const fs::path& target_mount_root) {
    if (!raw.is_object()) {
        throw ValidationError("profile must be an object");
    }
    reject_unknown_properties(
        raw,
        {"schemaVersion",
         "configurationGeneration",
         "profileId",
         "name",
         "enabled",
         "target",
         "paths",
         "settings",
         "hooks",
         "sources"},
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
    std::string configuration_generation = text(
        raw.value("configurationGeneration", ""),
        "configurationGeneration",
        true,
        32
    );
    if (!configuration_generation.empty() && !std::regex_match(configuration_generation, configuration_generation_re)) {
        throw ValidationError("configurationGeneration must contain 32 lowercase hexadecimal characters");
    }
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
    if (target.contains("mountPoint")) {
        (void)absolute_path(target.at("mountPoint"), "target.mountPoint");
    }
    if (input_schema_version == current_profile_schema_version && target.contains("mountUnit")) {
        throw ValidationError("target.mountUnit is application-controlled and cannot be changed");
    }
    if (target.contains("mountUnit") && !target.at("mountUnit").is_null() && target.at("mountUnit") != "") {
        (void)text(target.at("mountUnit"), "target.mountUnit", false, 256);
    }

    Json paths = object_or_empty(raw, "paths", "paths");
    if (input_schema_version == 1) {
        reject_unknown_properties(
            paths,
            {"remoteRoot", "incomingRoot", "stateDir", "statusRoot", "historyRoot"},
            "paths"
        );
        const std::vector<std::pair<std::string, std::string>> legacy_system_paths{
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
        {"dailyLimit",
         "incrementalRequired",
         "keepFailedLocalSnapshot",
         "autoEject",
         "remoteRetention",
         "localRetention",
         "minimumTargetFreeBytes",
         "minimumLocalFreeBytes"},
        "settings"
    );
    reject_unknown_properties(
        hooks,
        {"beforeSnapshot", "afterSnapshot"},
        "hooks"
    );
    const std::uint64_t remote_retention = integer_value(settings, "remoteRetention", "settings.remoteRetention", 30, RetentionCount::maximum);
    const std::uint64_t local_retention = integer_value(settings, "localRetention", "settings.localRetention", 30, RetentionCount::maximum);

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
        sources.push_back({{"id", source_id}, {"name", text(item.value("name", source_id), "sources[" + std::to_string(index) + "].name", false, 160)}, {"enabled", source_enabled}, {"subvolume", absolute_path(item.at("subvolume"), "sources[" + std::to_string(index) + "].subvolume")}, {"localSnapshotDir", local}, {"remoteSubdir", remote}, {"remoteRetention", integer_value(item, "remoteRetention", "sources[" + std::to_string(index) + "].remoteRetention", remote_retention, RetentionCount::maximum)}, {"localRetention", integer_value(item, "localRetention", "sources[" + std::to_string(index) + "].localRetention", local_retention, RetentionCount::maximum)}});
    }
    if (!any_enabled) {
        throw ValidationError("at least one source must be enabled");
    }

    Json result = {
        {"schemaVersion", current_profile_schema_version},
        {"profileId", profile_id},
        {"name", profile_name},
        {"enabled", enabled},
        {"target", {{"device", device}, {"luksUuid", luks_uuid}, {"btrfsUuid", btrfs_uuid}, {"partitionUuid", partition_uuid}, {"serial", serial}, {"mapperName", mapper_name}}},
        {"paths", {{"remoteRoot", remote_root}, {"incomingRoot", incoming_root}}},
        {"settings", {{"dailyLimit", boolean_value(settings, "dailyLimit", "settings.dailyLimit", true)}, {"incrementalRequired", boolean_value(settings, "incrementalRequired", "settings.incrementalRequired", true)}, {"keepFailedLocalSnapshot", boolean_value(settings, "keepFailedLocalSnapshot", "settings.keepFailedLocalSnapshot", false)}, {"autoEject", boolean_value(settings, "autoEject", "settings.autoEject", true)}, {"remoteRetention", remote_retention}, {"localRetention", local_retention}, {"minimumTargetFreeBytes", integer_value(settings, "minimumTargetFreeBytes", "settings.minimumTargetFreeBytes", 5LL * 1024 * 1024 * 1024, ByteThreshold::maximum)}, {"minimumLocalFreeBytes", integer_value(settings, "minimumLocalFreeBytes", "settings.minimumLocalFreeBytes", 1024LL * 1024 * 1024, ByteThreshold::maximum)}}},
        {"hooks", {{"beforeSnapshot", normalize_hook_commands(hooks, "beforeSnapshot", "hooks.beforeSnapshot")}, {"afterSnapshot", normalize_hook_commands(hooks, "afterSnapshot", "hooks.afterSnapshot")}}},
        {"sources", sources}
    };
    if (!configuration_generation.empty()) {
        result["configurationGeneration"] = configuration_generation;
    }
    return result;
}

ProfileDocument normalize_profile_document(const Json& raw, const fs::path& target_mount_root) {
    return ProfileDocument{normalize_profile(raw, target_mount_root)};
}

Profile profile_from_document(const ProfileDocument& document, const fs::path& target_mount_root) {
    const Json normalized = normalize_profile(document.value, target_mount_root);
    const Json& target = normalized.at("target");
    Profile profile{
        ProfileId{normalized.at("profileId").get<std::string>()},
        ProfileTarget{
            LuksUuid{target.at("luksUuid").get<std::string>()},
            BtrfsUuid{target.at("btrfsUuid").get<std::string>()},
            PartitionUuid{target.at("partitionUuid").get<std::string>()},
            MapperName{target.at("mapperName").get<std::string>()},
        },
        ProfilePaths{
            RemoteSnapshotRoot{normalized.at("paths").at("remoteRoot").get<std::string>()},
            IncomingRoot{normalized.at("paths").at("incomingRoot").get<std::string>()},
        },
    };
    profile.configuration_generation = normalized.value("configurationGeneration", "");
    profile.name = normalized.at("name").get<std::string>();
    profile.enabled = normalized.at("enabled").get<bool>();

    profile.target.device = target.at("device").get<std::string>();
    profile.target.serial = target.at("serial").get<std::string>();
    profile.target.mount_point = (normalized_absolute_path(target_mount_root, "TARGET_MOUNT_ROOT") / profile.id.value()).string();

    const Json& settings = normalized.at("settings");
    profile.settings.daily_limit = settings.at("dailyLimit").get<bool>();
    profile.settings.incremental_required = settings.at("incrementalRequired").get<bool>();
    profile.settings.keep_failed_local_snapshot = settings.at("keepFailedLocalSnapshot").get<bool>();
    profile.settings.auto_eject = settings.at("autoEject").get<bool>();
    profile.settings.remote_retention = RetentionCount{settings.at("remoteRetention").get<std::uint64_t>()};
    profile.settings.local_retention = RetentionCount{settings.at("localRetention").get<std::uint64_t>()};
    profile.settings.minimum_target_free_bytes = ByteThreshold{settings.at("minimumTargetFreeBytes").get<std::uint64_t>()};
    profile.settings.minimum_local_free_bytes = ByteThreshold{settings.at("minimumLocalFreeBytes").get<std::uint64_t>()};

    const Json& hooks = normalized.at("hooks");
    for (const Json& item : hooks.at("beforeSnapshot")) {
        profile.hooks.before_snapshot.push_back({
            .program = item.at("program").get<std::string>(),
            .arguments = item.at("arguments").get<std::vector<std::string>>(),
            .timeout = std::chrono::seconds{item.at("timeoutSeconds").get<std::chrono::seconds::rep>()},
        });
    }
    for (const Json& item : hooks.at("afterSnapshot")) {
        profile.hooks.after_snapshot.push_back({
            .program = item.at("program").get<std::string>(),
            .arguments = item.at("arguments").get<std::vector<std::string>>(),
            .timeout = std::chrono::seconds{item.at("timeoutSeconds").get<std::chrono::seconds::rep>()},
        });
    }

    for (const Json& item : normalized.at("sources")) {
        ProfileSource source{
            SourceId{item.at("id").get<std::string>()},
            SafeRelativePath{item.at("remoteSubdir").get<std::string>()},
        };
        source.name = item.at("name").get<std::string>();
        source.enabled = item.at("enabled").get<bool>();
        source.subvolume = item.at("subvolume").get<std::string>();
        source.local_snapshot_dir = item.at("localSnapshotDir").get<std::string>();
        source.remote_retention = RetentionCount{item.at("remoteRetention").get<std::uint64_t>()};
        source.local_retention = RetentionCount{item.at("localRetention").get<std::uint64_t>()};
        profile.sources.push_back(std::move(source));
    }
    return profile;
}

Profile profile_from_json(const Json& raw, const fs::path& target_mount_root) {
    return profile_from_document(normalize_profile_document(raw, target_mount_root), target_mount_root);
}

Json profile_to_json(const Profile& profile) {
    Json sources = Json::array();
    for (const ProfileSource& source : profile.sources) {
        sources.push_back({{"id", source.id.value()}, {"name", source.name}, {"enabled", source.enabled}, {"subvolume", source.subvolume}, {"localSnapshotDir", source.local_snapshot_dir}, {"remoteSubdir", source.remote_subdir.value().string()}, {"remoteRetention", source.remote_retention.value()}, {"localRetention", source.local_retention.value()}});
    }

    Json target = {
        {"device", profile.target.device},
        {"luksUuid", profile.target.luks_uuid.value()},
        {"btrfsUuid", profile.target.btrfs_uuid.value()},
        {"partitionUuid", profile.target.partition_uuid.value()},
        {"serial", profile.target.serial},
        {"mapperName", profile.target.mapper_name.value()}
    };

    auto hooks_to_json = [](const std::vector<ProfileHookCommand>& hooks) {
        Json result = Json::array();
        for (const ProfileHookCommand& hook : hooks) {
            result.push_back({{"type", "program"}, {"program", hook.program}, {"arguments", hook.arguments}, {"timeoutSeconds", hook.timeout.count()}});
        }
        return result;
    };

    Json result = {
        {"schemaVersion", current_profile_schema_version},
        {"profileId", profile.id.value()},
        {"name", profile.name},
        {"enabled", profile.enabled},
        {"target", target},
        {"paths", {{"remoteRoot", profile.paths.remote_root.value().string()}, {"incomingRoot", profile.paths.incoming_root.value().string()}}},
        {"settings", {{"dailyLimit", profile.settings.daily_limit}, {"incrementalRequired", profile.settings.incremental_required}, {"keepFailedLocalSnapshot", profile.settings.keep_failed_local_snapshot}, {"autoEject", profile.settings.auto_eject}, {"remoteRetention", profile.settings.remote_retention.value()}, {"localRetention", profile.settings.local_retention.value()}, {"minimumTargetFreeBytes", profile.settings.minimum_target_free_bytes.value()}, {"minimumLocalFreeBytes", profile.settings.minimum_local_free_bytes.value()}}},
        {"hooks", {{"beforeSnapshot", hooks_to_json(profile.hooks.before_snapshot)}, {"afterSnapshot", hooks_to_json(profile.hooks.after_snapshot)}}},
        {"sources", sources}
    };
    if (!profile.configuration_generation.empty()) {
        result["configurationGeneration"] = profile.configuration_generation;
    }
    return result;
}

ProfileDocument profile_to_document(const Profile& profile) {
    return ProfileDocument{profile_to_json(profile)};
}

} // namespace btrfsbackup::config
