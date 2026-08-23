#include <btrfsbackup/source_definition.hpp>

#include <filesystem>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>

namespace fs = std::filesystem;

namespace {

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string text(const btrfsbackup::Json& value, const std::string& name, bool allow_empty = false, std::size_t maximum = 512) {
    if (!value.is_string()) {
        throw btrfsbackup::ValidationError(name + " must be text");
    }
    std::string result = value.get<std::string>();
    if (result.find('\0') != std::string::npos || result.find('\n') != std::string::npos || result.find('\r') != std::string::npos) {
        throw btrfsbackup::ValidationError(name + " contains a forbidden control character");
    }
    if (result.size() > maximum) {
        throw btrfsbackup::ValidationError(name + " is too long");
    }
    if (!allow_empty && result.empty()) {
        throw btrfsbackup::ValidationError(name + " must not be empty");
    }
    return result;
}

std::string absolute_path(const btrfsbackup::Json& value, const std::string& name) {
    std::string result = text(value, name, false, 4096);
    if (!starts_with(result, "/")) {
        throw btrfsbackup::ValidationError(name + " must be an absolute path");
    }
    result = fs::path(result).lexically_normal().string();
    if (!starts_with(result, "/")) {
        throw btrfsbackup::ValidationError(name + " is invalid");
    }
    return result;
}

std::string relative_path(const btrfsbackup::Json& value, const std::string& name) {
    std::string result = text(value, name, false, 4096);
    fs::path path(result);
    if (path.is_absolute()) {
        throw btrfsbackup::ValidationError(name + " must be a safe relative path");
    }
    for (const auto& part : path) {
        std::string item = part.string();
        if (item.empty() || item == "." || item == "..") {
            throw btrfsbackup::ValidationError(name + " must be a safe relative path");
        }
    }
    return path.lexically_normal().string();
}

bool boolean_value(const btrfsbackup::Json& object, const std::string& key, const std::string& name, bool default_value) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    if (!object.at(key).is_boolean()) {
        throw btrfsbackup::ValidationError(name + " must be true or false");
    }
    return object.at(key).get<bool>();
}

long long integer_value(const btrfsbackup::Json& object, const std::string& key, const std::string& name, long long default_value) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    if (!object.at(key).is_number_integer()) {
        throw btrfsbackup::ValidationError(name + " must be an integer");
    }
    long long result = object.at(key).get<long long>();
    if (result < 0 || result > 100000) {
        throw btrfsbackup::ValidationError(name + " is outside the supported range");
    }
    return result;
}

std::vector<btrfsbackup::ProfileSource> profile_sources_from_json(const btrfsbackup::Json& root) {
    if (!root.is_object()) {
        throw btrfsbackup::ValidationError("profile must be an object");
    }
    if (!root.contains("sources") || !root.at("sources").is_array()) {
        throw btrfsbackup::ValidationError("sources must be an array");
    }
    if (root.at("sources").size() > 128) {
        throw btrfsbackup::ValidationError("at most 128 sources are supported");
    }

    if (root.contains("settings") && !root.at("settings").is_null() && !root.at("settings").is_object()) {
        throw btrfsbackup::ValidationError("settings must be an object");
    }
    const btrfsbackup::Json settings = root.contains("settings") && root.at("settings").is_object()
        ? root.at("settings")
        : btrfsbackup::Json::object();
    const long long remote_retention = integer_value(settings, "remoteRetention", "settings.remoteRetention", 30);
    const long long local_retention = integer_value(settings, "localRetention", "settings.localRetention", 30);

    std::set<std::string> seen_ids;
    std::set<std::string> seen_local;
    std::set<std::string> seen_remote;
    std::vector<btrfsbackup::ProfileSource> sources;

    for (std::size_t index = 0; index < root.at("sources").size(); ++index) {
        const btrfsbackup::Json& item = root.at("sources").at(index);
        const std::string prefix = "sources[" + std::to_string(index) + "]";
        if (!item.is_object()) {
            throw btrfsbackup::ValidationError(prefix + " must be an object");
        }

        std::string source_id = btrfsbackup::identifier(item.at("id"), prefix + ".id");
        if (!seen_ids.insert(source_id).second) {
            throw btrfsbackup::ValidationError("duplicate source id: " + source_id);
        }
        std::string local = absolute_path(item.at("localSnapshotDir"), prefix + ".localSnapshotDir");
        if (!seen_local.insert(local).second) {
            throw btrfsbackup::ValidationError("duplicate localSnapshotDir: " + local);
        }
        std::string remote = relative_path(item.value("remoteSubdir", source_id), prefix + ".remoteSubdir");
        if (!seen_remote.insert(remote).second) {
            throw btrfsbackup::ValidationError("duplicate remoteSubdir: " + remote);
        }

        sources.push_back({
            .id = source_id,
            .name = text(item.value("name", source_id), prefix + ".name", false, 160),
            .enabled = boolean_value(item, "enabled", prefix + ".enabled", true),
            .subvolume = absolute_path(item.at("subvolume"), prefix + ".subvolume"),
            .local_snapshot_dir = local,
            .remote_subdir = remote,
            .remote_retention = integer_value(item, "remoteRetention", prefix + ".remoteRetention", remote_retention),
            .local_retention = integer_value(item, "localRetention", prefix + ".localRetention", local_retention),
        });
    }

    return sources;
}

void write_source_record(
    std::ostream& output,
    const std::string& name,
    const std::string& subvolume,
    const std::string& local_snapshot_dir,
    const std::string& remote_subdir,
    long long remote_retention,
    long long local_retention
) {
    output << name << '\n'
           << subvolume << '\n'
           << local_snapshot_dir << '\n'
           << remote_subdir << '\n'
           << remote_retention << '\n'
           << local_retention << '\n';
}

} // namespace

namespace btrfsbackup {

void command_parse_profile_sources(const std::vector<std::string>& args, std::ostream& output) {
    fs::path profile_json;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--file") {
            profile_json = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown parse-profile-sources option: " + arg);
        }
    }

    if (profile_json.empty()) {
        throw ValidationError("parse-profile-sources requires --file");
    }

    for (const ProfileSource& source : profile_sources_from_json(load_json_file(profile_json))) {
        if (!source.enabled) {
            continue;
        }
        write_source_record(
            output,
            source.id,
            source.subvolume,
            source.local_snapshot_dir,
            source.remote_subdir,
            source.remote_retention,
            source.local_retention
        );
    }
}

} // namespace btrfsbackup
