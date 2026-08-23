#include <btrfsbackup/source_definition.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/shell_env.hpp>

namespace fs = std::filesystem;

namespace {

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

long long parse_uint(const std::string& option, const std::string& value) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw btrfsbackup::ValidationError(option + " must be an integer");
    }
    return std::stoll(value);
}

std::string require_absolute_path(const std::string& field, const std::string& value) {
    if (value.empty()) {
        throw btrfsbackup::ValidationError("missing required configuration variable: " + field);
    }
    if (value.front() != '/') {
        throw btrfsbackup::ValidationError(field + " must be an absolute path");
    }
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
        throw btrfsbackup::ValidationError(field + " contains a newline");
    }
    return value;
}

std::string require_relative_path(const std::string& field, const std::string& value) {
    if (value.empty()) {
        throw btrfsbackup::ValidationError("missing required configuration variable: " + field);
    }
    if (value.front() == '/') {
        throw btrfsbackup::ValidationError(field + " must be a safe relative path");
    }
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
        throw btrfsbackup::ValidationError(field + " contains a newline");
    }
    std::string wrapped = "/" + value + "/";
    if (wrapped.find("/../") != std::string::npos || wrapped.find("/./") != std::string::npos) {
        throw btrfsbackup::ValidationError(field + " must be a safe relative path");
    }
    return value;
}

} // namespace

namespace btrfsbackup {

SourceDefinition load_source_definition(
    const fs::path& source_config,
    long long default_remote_retention,
    long long default_local_retention
) {
    auto env = read_shell_environment(source_config);
    SourceDefinition source;
    source.enabled = env_bool(env, "ENABLED", true);
    if (!source.enabled) {
        return source;
    }

    source.name = env_required(env, "SOURCE_NAME");
    validate_identifier(source.name, "SOURCE_NAME");
    source.subvolume = require_absolute_path("SOURCE_SUBVOLUME", env_required(env, "SOURCE_SUBVOLUME"));
    source.local_snapshot_dir = require_absolute_path("LOCAL_SNAPSHOT_DIR", env_required(env, "LOCAL_SNAPSHOT_DIR"));
    source.remote_subdir = require_relative_path("REMOTE_SUBDIR", env_required(env, "REMOTE_SUBDIR"));
    source.remote_retention = env_int(env, "SOURCE_RETENTION_COUNT", default_remote_retention);
    source.local_retention = env_int(env, "SOURCE_LOCAL_RETENTION_COUNT", default_local_retention);
    return source;
}

void command_parse_source_definition(const std::vector<std::string>& args, std::ostream& output) {
    fs::path source_config;
    long long default_remote_retention = 30;
    long long default_local_retention = 30;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--file") {
            source_config = arg_value(args, i, arg);
        } else if (arg == "--remote-retention") {
            default_remote_retention = parse_uint(arg, arg_value(args, i, arg));
        } else if (arg == "--local-retention") {
            default_local_retention = parse_uint(arg, arg_value(args, i, arg));
        } else {
            throw ValidationError("unknown parse-source-definition option: " + arg);
        }
    }

    if (source_config.empty()) {
        throw ValidationError("parse-source-definition requires --file");
    }

    SourceDefinition source = load_source_definition(source_config, default_remote_retention, default_local_retention);
    if (!source.enabled) {
        output << "disabled\n";
        return;
    }

    output << "enabled\n"
           << source.name << '\n'
           << source.subvolume << '\n'
           << source.local_snapshot_dir << '\n'
           << source.remote_subdir << '\n'
           << source.remote_retention << '\n'
           << source.local_retention << '\n';
}

} // namespace btrfsbackup
