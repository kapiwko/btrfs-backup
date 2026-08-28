// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/manager_service.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include <core/errors.hpp>
#include <core/identifiers.hpp>
#include <config/model/profile.hpp>
#include <platform/linux/device_info.hpp>
#include <platform/linux/mount_info.hpp>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t max_document_bytes = 1024 * 1024;
constexpr std::size_t max_history_limit = 100;
constexpr std::size_t max_history_offset = 10000;

class UniqueFd {
  public:
    explicit UniqueFd(int value) : value_(value) {
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    ~UniqueFd() {
        if (value_ >= 0)
            close(value_);
    }
    [[nodiscard]] int get() const {
        return value_;
    }

  private:
    int value_;
};

btrfsbackup::config::Json read_bounded_json(const fs::path& path) {
    UniqueFd descriptor(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (descriptor.get() < 0) {
        throw btrfsbackup::ValidationError("cannot read manager data file " + path.string());
    }
    struct stat info{};
    if (fstat(descriptor.get(), &info) != 0 || !S_ISREG(info.st_mode)) {
        throw btrfsbackup::ValidationError("manager data path is not a regular file: " + path.string());
    }
    if (info.st_size < 0 || static_cast<std::uintmax_t>(info.st_size) > max_document_bytes) {
        throw btrfsbackup::ValidationError("manager data file exceeds the size limit: " + path.string());
    }
    if ((info.st_mode & 0022) != 0) {
        throw btrfsbackup::ValidationError("manager data file is writable by group or others: " + path.string());
    }

    std::string content;
    content.reserve(static_cast<std::size_t>(info.st_size));
    char buffer[8192];
    while (true) {
        const ssize_t count = read(descriptor.get(), buffer, sizeof(buffer));
        if (count > 0) {
            if (content.size() + static_cast<std::size_t>(count) > max_document_bytes) {
                throw btrfsbackup::ValidationError("manager data file exceeds the size limit: " + path.string());
            }
            content.append(buffer, static_cast<std::size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            throw btrfsbackup::ValidationError(
                "cannot read manager data file " + path.string() + ": " + std::strerror(errno)
            );
        }
    }
    try {
        return btrfsbackup::config::Json::parse(content);
    } catch (const std::exception& error) {
        throw btrfsbackup::ValidationError("invalid manager JSON " + path.string() + ": " + error.what());
    }
}

bool regular_file_without_symlink(const fs::directory_entry& entry) {
    std::error_code error;
    return entry.symlink_status(error).type() == fs::file_type::regular && !error;
}

bool regular_file_if_present(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return false;
    if (error)
        throw btrfsbackup::ValidationError("cannot inspect manager data file " + path.string());
    return status.type() == fs::file_type::regular;
}

btrfsbackup::config::Json unavailable_status() {
    return {
        {"schemaVersion", 3},
        {"state", "unavailable"},
        {"errorCode", ""},
        {"sourceName", ""},
        {"targetName", ""},
        {"speedBps", 0},
        {"etaSeconds", -1},
        {"sourceProgress", -1},
        {"overallProgress", -1},
        {"progressAccuracy", "indeterminate"},
    };
}

btrfsbackup::config::Json sanitize_public_status(const btrfsbackup::config::Json& input) {
    if (!input.is_object() || input.value("schemaVersion", 0) != 3) {
        throw btrfsbackup::ValidationError("public status has an unsupported schema");
    }
    btrfsbackup::config::Json result = unavailable_status();
    for (const char* field : {
             "state",
             "errorCode",
             "sourceName",
             "targetName",
             "speedBps",
             "etaSeconds",
             "sourceProgress",
             "overallProgress",
             "progressAccuracy",
         }) {
        if (!input.contains(field)) {
            throw btrfsbackup::ValidationError(std::string("public status is missing field: ") + field);
        }
        result[field] = input.at(field);
    }
    return result;
}

btrfsbackup::config::Json sanitize_private_history(const btrfsbackup::config::Json& input) {
    if (!input.is_object() || input.value("schemaVersion", 0) != 2) {
        throw btrfsbackup::ValidationError("private history has an unsupported schema");
    }
    const std::string state = input.value("state", "unavailable");
    const std::string detailed_error = input.value("errorCode", "");
    return {
        {"schemaVersion", 1},
        {"state", state},
        {"errorCode", detailed_error.empty() ? "" : (state == "cancelled" ? "backup.cancelled" : "backup.failed")},
        {"sourceName", input.value("currentSourceName", "")},
        {"targetName", input.value("targetName", "")},
        {"finishedAt", input.value("finishedAt", "")},
        {"overallProgress", input.value("overallProgress", -1)},
    };
}

std::vector<fs::path> history_paths(const fs::path& root, const std::string& profile_id) {
    const fs::path directory = root / profile_id;
    std::error_code error;
    if (!fs::is_directory(directory, error) || error)
        return {};

    std::vector<fs::path> result;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (error)
            break;
        const std::string name = entry.path().filename().string();
        if (name != "last.json" && entry.path().extension() == ".json" && regular_file_without_symlink(entry)) {
            result.push_back(entry.path());
        }
    }
    if (error)
        throw btrfsbackup::ValidationError("cannot enumerate history for profile " + profile_id);
    std::sort(result.rbegin(), result.rend());
    return result;
}

} // namespace

namespace btrfsbackup::daemon {

ManagerService::ManagerService(ManagerPaths paths) : paths_(std::move(paths)) {
}

btrfsbackup::config::Json ManagerService::get_capabilities() const {
    return {
        {"schemaVersion", 1},
        {"interface", "io.github.btrfsbackup.Manager1"},
        {"apiMajor", 1},
        {"apiMinor", 0},
        {"profileSchemaVersion", 3},
        {"publicStatusSchemaVersion", 3},
        {"historySchemaVersion", 1},
        {"deviceStateSchemaVersion", 1},
        {"readOnly", true},
        {"features", btrfsbackup::config::Json::array({"profiles", "status", "sanitized-history", "device-state"})},
    };
}

btrfsbackup::config::Json ManagerService::list_profiles() const {
    std::error_code error;
    if (!fs::is_directory(paths_.public_profile_root, error) || error)
        return btrfsbackup::config::Json::array();

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(paths_.public_profile_root, error)) {
        if (error)
            break;
        if (entry.path().extension() == ".json" && regular_file_without_symlink(entry))
            files.push_back(entry.path());
    }
    if (error)
        throw ValidationError("cannot enumerate public profiles");
    std::sort(files.begin(), files.end());

    btrfsbackup::config::Json result = btrfsbackup::config::Json::array();
    for (const fs::path& file : files) {
        btrfsbackup::config::Json profile = read_bounded_json(file);
        if (!profile.is_object() || profile.value("schemaVersion", 0) != 1) {
            throw ValidationError("public profile has an unsupported schema: " + file.string());
        }
        const std::string profile_id = profile.value("profileId", "");
        validate_profile_id(profile_id);
        btrfsbackup::config::Json sources = btrfsbackup::config::Json::array();
        if (!profile.contains("sources") || !profile.at("sources").is_array()) {
            throw ValidationError("public profile has invalid sources: " + file.string());
        }
        for (const btrfsbackup::config::Json& source : profile.at("sources")) {
            sources.push_back({{"id", source.value("id", "")}, {"name", source.value("name", "")}});
        }
        result.push_back({
            {"schemaVersion", 1},
            {"profileId", profile_id},
            {"name", profile.value("name", "")},
            {"targetName", profile.value("target", btrfsbackup::config::Json::object()).value("name", "")},
            {"sources", std::move(sources)},
        });
    }
    return result;
}

btrfsbackup::config::Json ManagerService::get_status(const std::string& profile_id) const {
    validate_profile_id(profile_id);
    const fs::path current = paths_.status_root / profile_id / "current.json";
    if (regular_file_if_present(current))
        return sanitize_public_status(read_bounded_json(current));

    const fs::path last = paths_.history_root / profile_id / "last.json";
    if (regular_file_if_present(last)) {
        btrfsbackup::config::Json history = sanitize_private_history(read_bounded_json(last));
        btrfsbackup::config::Json result = unavailable_status();
        for (const char* field : {"state", "errorCode", "sourceName", "targetName", "overallProgress"}) {
            result[field] = history.at(field);
        }
        return result;
    }
    return unavailable_status();
}

btrfsbackup::config::Json ManagerService::get_history_sanitized(
    const std::string& profile_id,
    std::size_t offset,
    std::size_t limit
) const {
    validate_profile_id(profile_id);
    if (limit == 0 || limit > max_history_limit) {
        throw ValidationError("history limit must be between 1 and 100");
    }
    if (offset > max_history_offset)
        throw ValidationError("history offset exceeds 10000");

    const std::vector<fs::path> files = history_paths(paths_.history_root, profile_id);
    btrfsbackup::config::Json result = btrfsbackup::config::Json::array();
    if (offset >= files.size())
        return result;
    const std::size_t end = std::min(files.size(), offset + limit);
    for (std::size_t index = offset; index < end; ++index) {
        result.push_back(sanitize_private_history(read_bounded_json(files[index])));
    }
    return result;
}

btrfsbackup::config::Json ManagerService::get_device_state(const std::string& profile_id) const {
    validate_profile_id(profile_id);
    const fs::path profile_path = paths_.config_root / "profiles" / profile_id / "profile.json";
    const btrfsbackup::config::Profile profile = btrfsbackup::config::profile_from_json(read_bounded_json(profile_path), paths_.target_mount_root);
    const fs::path mapper = btrfsbackup::platform::linux::mapper_path(profile.target.mapper_name, paths_.mapper_root);
    const fs::path mountpoint = paths_.target_mount_root / profile_id;
    const std::vector<btrfsbackup::backup::MountEntry> mounts = btrfsbackup::platform::linux::read_mount_table(paths_.mountinfo_path);
    const bool mounted = btrfsbackup::backup::mount_at(mounts, mountpoint).has_value();
    const bool unlocked = fs::exists(mapper);
    const bool connected = fs::exists(profile.target.device);

    std::string state = "disconnected";
    bool safe_to_remove = false;
    if (mounted) {
        state = "mounted";
    } else if (unlocked) {
        state = "unlocked";
    } else if (connected) {
        state = "connected";
        safe_to_remove = true;
    }
    return {
        {"schemaVersion", 1},
        {"profileId", profile_id},
        {"targetName", profile.target.mapper_name},
        {"state", state},
        {"connected", connected},
        {"unlocked", unlocked},
        {"mounted", mounted},
        {"safeToRemove", safe_to_remove},
    };
}

} // namespace btrfsbackup::daemon
