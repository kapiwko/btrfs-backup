// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/application_config.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include <config/errors.hpp>
#include <platform/linux/trusted_file.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

fs::path absolute_path(const std::string& value, const std::string& name) {
    if (value.empty() || value.find('\0') != std::string::npos || value.find('\r') != std::string::npos) {
        throw ValidationError(name + " must be a non-empty absolute path");
    }
    fs::path result = value;
    if (!result.is_absolute()) {
        throw ValidationError(name + " must be an absolute path");
    }
    return result.lexically_normal();
}

std::map<std::string, std::string> parse_config(const std::string& content) {
    const std::set<std::string> allowed{
        "CONFIG_VERSION",
        "STATE_ROOT",
        "STATUS_ROOT",
        "HISTORY_ROOT",
        "TARGET_MOUNT_ROOT"
    };
    std::map<std::string, std::string> result;
    std::istringstream input(content);
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0) {
            throw ValidationError("invalid application configuration line " + std::to_string(line_number));
        }
        std::string key = line.substr(0, separator);
        std::string value = line.substr(separator + 1);
        if (!allowed.contains(key)) {
            throw ValidationError("application configuration key " + key + " is not supported");
        }
        if (!result.emplace(std::move(key), std::move(value)).second) {
            throw ValidationError("duplicate application configuration key on line " + std::to_string(line_number));
        }
    }
    return result;
}

} // namespace

ApplicationConfig::ApplicationConfig() : ApplicationConfig(defaults().paths()) {
}

ApplicationConfig::ApplicationConfig(ApplicationPaths paths) : paths_(std::move(paths)) {
}

ApplicationConfig ApplicationConfig::defaults() {
    return ApplicationConfig({
        .state_root = "/var/lib/btrfs-backup",
        .status_root = "/run/btrfs-backup/profiles",
        .history_root = "/var/lib/btrfs-backup/history",
        .target_mount_root = "/mnt/btrfs-backup",
    });
}

ApplicationConfig ApplicationConfig::load(const fs::path& config_root) {
    ApplicationPaths result = defaults().paths();
    const bool system_config = fs::absolute(config_root).lexically_normal() == fs::path("/etc/btrfs-backup");
    const fs::path config_path = system_config ? fs::path("/etc/btrfs-backup.conf") : config_root / "btrfs-backup.conf";
    std::error_code error;
    fs::file_status status = fs::symlink_status(config_path, error);
    if (error == std::errc::no_such_file_or_directory || (!error && !fs::exists(status))) {
        return ApplicationConfig(std::move(result));
    }
    if (error) {
        throw ValidationError("cannot inspect application configuration " + config_path.string() + ": " + error.message());
    }

    TrustedFilePolicy policy{
        .allow_current_user_owner = !system_config,
        .allow_group_other_read = true,
    };
    const auto values = parse_config(read_trusted_config_file(config_path, policy));
    auto version = values.find("CONFIG_VERSION");
    if (version == values.end() || version->second != "1") {
        throw ValidationError("application configuration CONFIG_VERSION must be 1");
    }
    if (auto value = values.find("STATE_ROOT"); value != values.end())
        result.state_root = absolute_path(value->second, "STATE_ROOT");
    if (auto value = values.find("STATUS_ROOT"); value != values.end())
        result.status_root = absolute_path(value->second, "STATUS_ROOT");
    if (auto value = values.find("HISTORY_ROOT"); value != values.end())
        result.history_root = absolute_path(value->second, "HISTORY_ROOT");
    if (auto value = values.find("TARGET_MOUNT_ROOT"); value != values.end())
        result.target_mount_root = absolute_path(value->second, "TARGET_MOUNT_ROOT");
    return ApplicationConfig(std::move(result));
}

const ApplicationPaths& ApplicationConfig::paths() const {
    return paths_;
}

} // namespace btrfsbackup
