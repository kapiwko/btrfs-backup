// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemBrowseSessionBackend.hpp>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <backup/planning/BackupPreflightValidation.hpp>
#include <config/ProfileRender.hpp>
#include <config/json/JsonIo.hpp>
#include <config/domain/Validation.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/filesystem/FileIo.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using btrfsbackup::platform::linux::OwnedFileDescriptor;

[[noreturn]] void target_error(const std::string& message) {
    throw dbus::ManagerOperationError(dbus::ManagerErrorCode::TargetUnavailable, message);
}

void require_root_directory(const fs::path& path) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error)
        target_error("cannot create browse session directory");
    struct stat status{};
    if (lstat(path.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != 0)
        target_error("browse session root is not a trusted root-owned directory");
    chmod(path.c_str(), 0711);
}

void require_private_directory(const fs::path& path, mode_t mode, uid_t owner) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error)
        target_error("cannot create browse session directory");
    struct stat status{};
    if (lstat(path.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode))
        target_error("browse session directory is not trusted");
    if (status.st_uid != owner && chown(path.c_str(), owner, static_cast<gid_t>(-1)) != 0)
        target_error("cannot assign browse session directory owner");
    if (chmod(path.c_str(), mode) != 0)
        target_error("cannot set browse session directory permissions");
}

[[noreturn]] void browse_path_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

std::vector<std::string> validated_components(const fs::path& relative) {
    if (relative.is_absolute())
        throw std::invalid_argument("browse path must be relative");
    std::vector<std::string> result;
    for (const fs::path& component : relative) {
        const std::string value = component.string();
        if (value.empty() || value == ".")
            continue;
        if (value == ".." || value.find('/') != std::string::npos || value.find('\0') != std::string::npos)
            throw std::invalid_argument("browse path contains an unsafe component");
        result.push_back(value);
    }
    return result;
}

OwnedFileDescriptor open_beneath(int root, const fs::path& relative, int final_flags) {
    OwnedFileDescriptor current(openat(root, ".", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!current.valid())
        browse_path_error("cannot open browse root");
    const auto components = validated_components(relative);
    if (components.empty()) {
        OwnedFileDescriptor result(openat(current.get(), ".", final_flags | O_CLOEXEC | O_NOFOLLOW));
        if (!result.valid())
            browse_path_error("cannot open browse entry");
        return result;
    }
    for (std::size_t index = 0; index + 1 < components.size(); ++index) {
        OwnedFileDescriptor next(openat(
            current.get(), components[index].c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        ));
        if (!next.valid())
            browse_path_error("cannot traverse browse entry");
        current = std::move(next);
    }
    OwnedFileDescriptor result(openat(
        current.get(), components.back().c_str(), final_flags | O_CLOEXEC | O_NOFOLLOW
    ));
    if (!result.valid())
        browse_path_error("cannot open browse entry");
    return result;
}

struct DirectoryCloser {
    void operator()(DIR* directory) const noexcept {
        if (directory != nullptr)
            closedir(directory);
    }
};

BrowseEntryInfo entry_info(int parent, const std::string& name) {
    struct stat status{};
    if (fstatat(parent, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
        browse_path_error("cannot inspect browse entry");
    if (S_ISLNK(status.st_mode) || (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode)))
        throw std::invalid_argument("browse entry has an unsupported type");
    return {
        name,
        S_ISDIR(status.st_mode),
        S_ISREG(status.st_mode) ? static_cast<std::uint64_t>(status.st_size) : 0,
        static_cast<std::uint32_t>(status.st_mode),
        status.st_mtim.tv_sec,
    };
}

} // namespace

SystemBrowseSessionBackend::SystemBrowseSessionBackend(
    btrfsbackup::config::IProfileRepository& profiles,
    btrfsbackup::backup::IMountInspector& mounts,
    ISystemdUnitController& units,
    fs::path session_root,
    fs::path lock_root
) : profiles_(profiles), mounts_(mounts), units_(units), session_root_(std::move(session_root)), lock_root_(std::move(lock_root)) {
}

SystemBrowseSessionBackend::~SystemBrowseSessionBackend() noexcept {
    for (auto session = sessions_.begin(); session != sessions_.end();) {
        auto current = session++;
        try {
            close(BrowseSessionId{current->first});
        } catch (...) {}
    }
}

SystemBrowseSessionBackend::TargetLease& SystemBrowseSessionBackend::acquire_target(
    const btrfsbackup::config::Profile& profile
) {
    const std::string key = profile.target.luks_uuid.value();
    if (auto existing = targets_.find(key); existing != targets_.end()) {
        ++existing->second->users;
        return *existing->second;
    }

    auto lease = std::make_unique<TargetLease>(TargetLease{
        profile,
        btrfsbackup::platform::linux::filesystem::FileLock{
            btrfsbackup::platform::linux::filesystem::target_lock_path(lock_root_, profile.target.luks_uuid)
        },
        1,
        false,
    });
    if (!lease->lock.try_acquire())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "backup target is in use");

    auto table = mounts_.inspect();
    if (!btrfsbackup::backup::mount_at(table, profile.target.mount_point).has_value()) {
        const auto result = units_.start_unit({
            btrfsbackup::config::target_mount_unit_name(profile.target.mount_point),
            std::chrono::minutes(2),
        });
        if (!result)
            target_error("cannot mount backup target for browsing");
        lease->mounted_by_backend = true;
        table = mounts_.inspect();
    }
    try {
        (void)btrfsbackup::backup::planning::validate_backup_target_mount(
            profile,
            table,
            btrfsbackup::backup::planning::TargetMountAccess::Read
        );
    } catch (...) {
        if (lease->mounted_by_backend)
            (void)units_.stop_unit({btrfsbackup::config::target_mount_unit_name(profile.target.mount_point), std::chrono::minutes(2)});
        throw;
    }
    lease->lock.downgrade_to_shared();
    auto [position, inserted] = targets_.emplace(key, std::move(lease));
    (void)inserted;
    return *position->second;
}

void SystemBrowseSessionBackend::mount_read_only(
    const BrowseSessionId& session_id,
    const fs::path& source,
    const fs::path& target
) {
    const auto result = units_.start_transient_unit({
        .unit = "btrfs-backup-browse-mount-" + std::string(session_id.value()) + ".service",
        .command = {"mount", "-o", "bind,ro,nodev,nosuid,noexec,nosymfollow", source.string(), target.string()},
        .properties = {"PrivateMounts=no"},
        .environment = {},
        .timeout = std::chrono::seconds(30),
        .wait = true,
    });
    if (!result)
        target_error("cannot create read-only browse bind mount");

    const auto verified = units_.start_transient_unit({
        .unit = "btrfs-backup-browse-verify-" + std::string(session_id.value()) + ".service",
        .command = {"findmnt", "--noheadings", "--mountpoint", target.string(), "--options", "ro,nodev,nosuid,noexec,nosymfollow"},
        .properties = {"PrivateMounts=no"},
        .environment = {},
        .timeout = std::chrono::seconds(30),
        .wait = true,
    });
    if (!verified) {
        try {
            unmount(session_id, target);
        } catch (...) {}
        target_error("browse bind mount failed read-only verification");
    }
}

void SystemBrowseSessionBackend::unmount(const BrowseSessionId& session_id, const fs::path& target) {
    if (!btrfsbackup::backup::mount_at(mounts_.inspect(), target).has_value())
        return;
    const auto result = units_.start_transient_unit({
        .unit = "btrfs-backup-browse-unmount-" + std::string(session_id.value()) + ".service",
        .command = {"umount", target.string()},
        .properties = {"PrivateMounts=no"},
        .environment = {},
        .timeout = std::chrono::seconds(30),
        .wait = true,
    });
    if (!result)
        target_error("cannot unmount browse session");
}

void SystemBrowseSessionBackend::write_marker(const BrowseSessionId& id, const SessionMount& mount) {
    platform::linux::filesystem::atomic_write(
        mount.marker,
        config::json::dump_json({
            {"schemaVersion", 1},
            {"sessionId", id.value()},
            {"callerUid", mount.caller_uid},
            {"targetKey", mount.target_key},
            {"targetUnit", mount.target_unit},
            {"directory", mount.directory.string()},
            {"view", mount.view.string()},
            {"viewMounted", mount.view_mounted},
            {"targetMountedByBackend", mount.target_mounted_by_backend},
            {"targetReleased", mount.target_released},
        }),
        0600
    );
}

std::optional<SystemBrowseSessionBackend::SessionMount> SystemBrowseSessionBackend::read_marker(
    const fs::path& marker
) const {
    try {
        struct stat status{};
        if (lstat(marker.c_str(), &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != 0)
            return std::nullopt;
        const config::json::Json document = config::json::load_json_file(marker);
        if (document.at("schemaVersion").get<int>() != 1)
            return std::nullopt;
        const std::uint32_t uid = document.at("callerUid").get<std::uint32_t>();
        const fs::path expected_directory = session_root_ / std::to_string(uid) / marker.stem();
        const fs::path directory = fs::path(document.at("directory").get<std::string>()).lexically_normal();
        const fs::path view = fs::path(document.at("view").get<std::string>()).lexically_normal();
        if (directory != expected_directory || view != directory / "repository")
            return std::nullopt;
        return SessionMount{
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

OpenedBrowseRoot SystemBrowseSessionBackend::open(
    const ProfileId& profile_id,
    const BrowseSessionId& session_id,
    std::uint32_t caller_uid
) {
    require_root_directory(session_root_);
    const fs::path state_root = session_root_ / ".state";
    require_private_directory(state_root, 0700, 0);
    const auto loaded = profiles_.get(profile_id);
    const TargetLease& target = acquire_target(loaded.profile);
    const std::string key{loaded.profile.target.luks_uuid.value()};
    const fs::path uid_root = session_root_ / std::to_string(caller_uid);
    require_private_directory(uid_root, 0700, caller_uid);
    const fs::path directory = uid_root / std::string(session_id.value());
    const fs::path view = directory / "repository";
    SessionMount record{
        .target_key = key,
        .target_unit = btrfsbackup::config::target_mount_unit_name(loaded.profile.target.mount_point),
        .directory = directory,
        .view = view,
        .marker = state_root / (std::string(session_id.value()) + ".json"),
        .caller_uid = caller_uid,
        .target_mounted_by_backend = target.mounted_by_backend,
    };
    auto [session, inserted] = sessions_.emplace(std::string(session_id.value()), record);
    if (!inserted) {
        release_target(key);
        target_error("browse session already exists");
    }
    try {
        if (fs::exists(directory))
            target_error("browse session directory already exists");
        fs::create_directories(view);
        if (chown(directory.c_str(), caller_uid, static_cast<gid_t>(-1)) != 0 ||
            chown(view.c_str(), caller_uid, static_cast<gid_t>(-1)) != 0 ||
            chmod(directory.c_str(), 0500) != 0 || chmod(view.c_str(), 0500) != 0)
            target_error("cannot secure browse session directory");
        write_marker(session_id, session->second);
        session->second.view_mounted = true;
        write_marker(session_id, session->second);
        mount_read_only(session_id, loaded.profile.paths.remote_root.value(), view);
        return {view};
    } catch (...) {
        try {
            close(session_id);
        } catch (...) {}
        throw;
    }
}

void SystemBrowseSessionBackend::release_target(const std::string& target_key) {
    auto target = targets_.find(target_key);
    if (target == targets_.end())
        return;
    if (target->second->users > 1) {
        --target->second->users;
        return;
    }
    if (target->second->mounted_by_backend) {
        if (!target->second->lock.try_upgrade_to_exclusive())
            target_error("cannot acquire exclusive target lock for cleanup");
        const auto& profile = target->second->profile;
        const auto result = units_.stop_unit({btrfsbackup::config::target_mount_unit_name(profile.target.mount_point), std::chrono::minutes(2)});
        if (!result)
            target_error("cannot unmount backup target after browsing");
    }
    targets_.erase(target);
}

void SystemBrowseSessionBackend::cleanup_record(
    const BrowseSessionId& session_id,
    SessionMount& mount,
    bool release_live_target
) {
    if (mount.view_mounted) {
        unmount(session_id, mount.view);
        mount.view_mounted = false;
        write_marker(session_id, mount);
    }
    std::error_code error;
    fs::remove_all(mount.directory, error);
    if (error)
        target_error("cannot remove browse session directory");
    if (!mount.target_released) {
        if (release_live_target) {
            release_target(mount.target_key);
        } else if (mount.target_mounted_by_backend) {
            if (targets_.contains(mount.target_key))
                target_error("stale target cleanup is deferred while the target is in use");
            const auto result = units_.stop_unit({mount.target_unit, std::chrono::minutes(2)});
            if (!result)
                target_error("cannot unmount backup target after stale browse session");
        }
        mount.target_released = true;
        write_marker(session_id, mount);
    }
    if (!fs::remove(mount.marker, error) || error)
        target_error("cannot remove browse session cleanup marker");
}

void SystemBrowseSessionBackend::close(const BrowseSessionId& session_id) {
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        return;
    cleanup_record(session_id, session->second, true);
    sessions_.erase(session);
}

void SystemBrowseSessionBackend::cleanup_stale() {
    if (!fs::exists(session_root_))
        return;
    require_root_directory(session_root_);
    const fs::path state_root = session_root_ / ".state";
    require_private_directory(state_root, 0700, 0);
    std::vector<std::pair<BrowseSessionId, SessionMount>> records;
    for (const auto& entry : fs::directory_iterator(state_root)) {
        if (entry.is_symlink() || !entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        if (sessions_.contains(entry.path().stem().string()))
            continue;
        auto record = read_marker(entry.path());
        if (!record.has_value())
            continue;
        records.emplace_back(BrowseSessionId{entry.path().stem().string()}, std::move(*record));
    }
    std::set<std::string> targets_with_mounted_views;
    for (auto& [id, record] : records) {
        if (record.view_mounted) {
            try {
                unmount(id, record.view);
                record.view_mounted = false;
                write_marker(id, record);
            } catch (...) {}
        }
        if (record.view_mounted)
            targets_with_mounted_views.insert(record.target_key);
    }
    for (auto& [id, record] : records) {
        if (record.view_mounted || targets_with_mounted_views.contains(record.target_key))
            continue;
        try {
            cleanup_record(id, record, false);
        } catch (...) {}
    }
}

std::vector<BrowseEntryInfo> SystemBrowseSessionBackend::list_directory(
    const BrowseSessionId& session_id,
    const fs::path& relative_path,
    std::size_t maximum_entries
) {
    const auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    OwnedFileDescriptor root(::open(session->second.view.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!root.valid())
        browse_path_error("cannot open browse session view");
    OwnedFileDescriptor directory = open_beneath(root.get(), relative_path, O_RDONLY | O_DIRECTORY);
    const int duplicate = dup(directory.get());
    if (duplicate < 0)
        browse_path_error("cannot duplicate browse directory");
    std::unique_ptr<DIR, DirectoryCloser> stream(fdopendir(duplicate));
    if (!stream) {
        ::close(duplicate);
        browse_path_error("cannot open browse directory stream");
    }
    std::vector<BrowseEntryInfo> result;
    errno = 0;
    while (dirent* item = readdir(stream.get())) {
        const std::string name = item->d_name;
        if (name == "." || name == ".." || name == ".incoming")
            continue;
        try {
            BrowseEntryInfo info = entry_info(directory.get(), name);
            if (result.size() >= maximum_entries)
                throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "browse directory exceeds the safe entry limit");
            result.push_back(std::move(info));
        } catch (const std::invalid_argument&) {
            continue;
        }
        errno = 0;
    }
    if (errno != 0)
        browse_path_error("cannot read browse directory");
    return result;
}

BrowseEntryInfo SystemBrowseSessionBackend::inspect_entry(
    const BrowseSessionId& session_id,
    const fs::path& relative_path
) {
    const auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    OwnedFileDescriptor root(::open(session->second.view.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!root.valid())
        browse_path_error("cannot open browse session view");
    OwnedFileDescriptor entry = open_beneath(root.get(), relative_path, O_PATH);
    struct stat status{};
    if (fstat(entry.get(), &status) != 0)
        browse_path_error("cannot inspect browse entry");
    if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode))
        throw std::invalid_argument("browse entry has an unsupported type");
    return {
        relative_path.filename().string(),
        S_ISDIR(status.st_mode),
        S_ISREG(status.st_mode) ? static_cast<std::uint64_t>(status.st_size) : 0,
        static_cast<std::uint32_t>(status.st_mode),
        status.st_mtim.tv_sec,
    };
}

OwnedFileDescriptor SystemBrowseSessionBackend::open_file(
    const BrowseSessionId& session_id,
    const fs::path& relative_path
) {
    const auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    OwnedFileDescriptor root(::open(session->second.view.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!root.valid())
        browse_path_error("cannot open browse session view");
    OwnedFileDescriptor result = open_beneath(root.get(), relative_path, O_RDONLY | O_NONBLOCK);
    struct stat status{};
    if (fstat(result.get(), &status) != 0)
        browse_path_error("cannot inspect browse file");
    if (!S_ISREG(status.st_mode))
        throw std::invalid_argument("browse entry is not a regular file");
    return result;
}

std::vector<BackupCoverage> SystemBrowseSessionBackend::resolve_coverage(
    const fs::path& local_path,
    const std::vector<ProfileId>& profile_ids
) {
    std::vector<BackupCoverage> result;
    for (const ProfileId& profile_id : profile_ids) {
        const auto loaded = profiles_.get(profile_id);
        const btrfsbackup::config::ProfileSource* best = nullptr;
        for (const auto& source : loaded.profile.sources) {
            if (!source.enabled || !btrfsbackup::config::path_is_within(local_path, source.subvolume.value()))
                continue;
            if (best == nullptr || source.subvolume.value().string().size() > best->subvolume.value().string().size())
                best = &source;
        }
        if (best == nullptr)
            continue;
        const fs::path relative = local_path.lexically_relative(best->subvolume.value());
        result.push_back({
            std::string(profile_id.value()),
            std::string(best->id.value()),
            relative.empty() ? "." : relative.string(),
        });
    }
    return result;
}

} // namespace btrfsbackup::daemon::control
