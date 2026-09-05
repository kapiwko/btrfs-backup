// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseSessionMountStore.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <system_error>

#include <config/json/JsonIo.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/filesystem/FileIo.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

[[noreturn]] void store_error(const std::string& message) {
    throw dbus::ManagerOperationError(dbus::ManagerErrorCode::TargetUnavailable, message);
}

mode_t native_mode(fs::perms mode) {
    return static_cast<mode_t>(mode);
}

} // namespace

BrowseSessionMountStore::BrowseSessionMountStore(fs::path session_root, std::uint32_t trusted_uid)
    : session_root_(std::move(session_root)), trusted_uid_(trusted_uid) {
}

void BrowseSessionMountStore::require_root_directory(const fs::path& path) const {
    std::error_code error;
    fs::create_directories(path, error);
    if (error)
        store_error("cannot create browse session directory");
    struct stat status{};
    if (lstat(path.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != static_cast<uid_t>(trusted_uid_))
        store_error("browse session root is not a trusted root-owned directory");
    chmod(path.c_str(), 0711);
}

void BrowseSessionMountStore::require_private_directory(const fs::path& path, fs::perms mode) const {
    std::error_code error;
    fs::create_directories(path, error);
    if (error)
        store_error("cannot create browse session directory");
    struct stat status{};
    if (lstat(path.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode))
        store_error("browse session directory is not trusted");
    const uid_t owner = static_cast<uid_t>(trusted_uid_);
    if (status.st_uid != owner && chown(path.c_str(), owner, static_cast<gid_t>(-1)) != 0)
        store_error("cannot assign browse session directory owner");
    if (chmod(path.c_str(), native_mode(mode)) != 0)
        store_error("cannot set browse session directory permissions");
}

void BrowseSessionMountStore::prepare_root() const {
    require_root_directory(session_root_);
    require_private_directory(session_root_ / ".state", fs::perms::owner_all);
}

BrowseSessionMountRecord BrowseSessionMountStore::make_record(
    const BrowseSessionId& session_id,
    std::uint32_t caller_uid,
    std::string target_key,
    std::string target_unit,
    bool target_mounted_by_backend
) const {
    const fs::path uid_root = session_root_ / std::to_string(caller_uid);
    require_private_directory(uid_root, fs::perms::owner_all);
    const fs::path directory = uid_root / std::string(session_id.value());
    return {
        .target_key = std::move(target_key),
        .target_unit = std::move(target_unit),
        .directory = directory,
        .view = directory / "repository",
        .marker = session_root_ / ".state" / (std::string(session_id.value()) + ".json"),
        .caller_uid = caller_uid,
        .view_mounted = false,
        .target_mounted_by_backend = target_mounted_by_backend,
        .target_released = false,
    };
}

void BrowseSessionMountStore::write(
    const BrowseSessionId& session_id,
    const BrowseSessionMountRecord& record
) const {
    platform::linux::filesystem::atomic_write(
        record.marker,
        config::json::dump_json({
            {"schemaVersion", 1},
            {"sessionId", session_id.value()},
            {"callerUid", record.caller_uid},
            {"targetKey", record.target_key},
            {"targetUnit", record.target_unit},
            {"directory", record.directory.string()},
            {"view", record.view.string()},
            {"viewMounted", record.view_mounted},
            {"targetMountedByBackend", record.target_mounted_by_backend},
            {"targetReleased", record.target_released},
        }),
        0600
    );
}

std::optional<BrowseSessionMountRecord> BrowseSessionMountStore::read(const fs::path& marker) const {
    try {
        struct stat status{};
        if (lstat(marker.c_str(), &status) != 0 || !S_ISREG(status.st_mode) ||
            status.st_uid != static_cast<uid_t>(trusted_uid_))
            return std::nullopt;
        const config::json::Json document = config::json::load_json_file(marker);
        if (document.at("schemaVersion").get<int>() != 1)
            return std::nullopt;
        const std::string session_id = document.at("sessionId").get<std::string>();
        if (session_id.empty() || marker.stem() != session_id)
            return std::nullopt;
        const std::uint32_t uid = document.at("callerUid").get<std::uint32_t>();
        const fs::path expected_directory = session_root_ / std::to_string(uid) / marker.stem();
        const fs::path directory = fs::path(document.at("directory").get<std::string>()).lexically_normal();
        const fs::path view = fs::path(document.at("view").get<std::string>()).lexically_normal();
        if (directory != expected_directory || view != directory / "repository")
            return std::nullopt;
        return BrowseSessionMountRecord{
            .target_key = document.at("targetKey").get<std::string>(),
            .target_unit = document.at("targetUnit").get<std::string>(),
            .directory = directory,
            .view = view,
            .marker = marker,
            .caller_uid = uid,
            .view_mounted = document.at("viewMounted").get<bool>(),
            .target_mounted_by_backend = document.at("targetMountedByBackend").get<bool>(),
            .target_released = document.at("targetReleased").get<bool>(),
        };
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::pair<BrowseSessionId, BrowseSessionMountRecord>> BrowseSessionMountStore::stale_records(
    const std::set<std::string>& live_session_ids
) const {
    std::vector<std::pair<BrowseSessionId, BrowseSessionMountRecord>> records;
    if (!fs::exists(session_root_))
        return records;
    prepare_root();
    for (const auto& entry : fs::directory_iterator(session_root_ / ".state")) {
        if (entry.is_symlink() || !entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        if (live_session_ids.contains(entry.path().stem().string()))
            continue;
        auto record = read(entry.path());
        if (!record.has_value())
            continue;
        records.emplace_back(BrowseSessionId{entry.path().stem().string()}, std::move(*record));
    }
    return records;
}

void BrowseSessionMountStore::remove_session_directory(const BrowseSessionMountRecord& record) const {
    std::error_code error;
    fs::remove_all(record.directory, error);
    if (error)
        store_error("cannot remove browse session directory");
}

void BrowseSessionMountStore::remove_marker(const BrowseSessionMountRecord& record) const {
    std::error_code error;
    if (!fs::remove(record.marker, error) || error)
        store_error("cannot remove browse session cleanup marker");
}

const fs::path& BrowseSessionMountStore::root() const noexcept {
    return session_root_;
}

} // namespace btrfsbackup::daemon::control
