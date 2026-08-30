// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemBrowseSessionBackend.hpp>

#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <system_error>

#include <backup/planning/BackupPreflightValidation.hpp>
#include <config/ProfileRender.hpp>
#include <config/domain/Validation.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

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

bool has_option(const std::string& options, const std::string& option) {
    return ("," + options + ",").contains("," + option + ",");
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
    while (!sessions_.empty()) {
        try { close(BrowseSessionId{sessions_.begin()->first}); }
        catch (...) { sessions_.erase(sessions_.begin()); }
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
        (void)btrfsbackup::backup::planning::validate_backup_target_mount(profile, table);
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

void SystemBrowseSessionBackend::mount_read_only(const fs::path& source, const fs::path& target) {
    if (::mount(source.c_str(), target.c_str(), nullptr, MS_BIND, nullptr) != 0)
        target_error("cannot create browse bind mount");
    constexpr unsigned long flags = MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NODEV | MS_NOSUID | MS_NOEXEC;
    if (::mount(nullptr, target.c_str(), nullptr, flags, "nosymfollow") != 0) {
        (void)::umount2(target.c_str(), MNT_DETACH);
        target_error("cannot make browse bind mount read-only");
    }
}

void SystemBrowseSessionBackend::unmount(const fs::path& target) {
    if (::umount2(target.c_str(), 0) != 0 && errno != EINVAL && errno != ENOENT)
        target_error("cannot unmount browse session");
}

void SystemBrowseSessionBackend::write_marker(
    const BrowseSessionId& id, const ProfileId& profile, std::uint32_t uid, const SessionMount& mount
) {
    std::ofstream marker(mount.directory / ".btrfs-backup-session", std::ios::trunc);
    if (!marker)
        target_error("cannot write browse session marker");
    marker << id.value() << '\n' << profile.value() << '\n' << uid << '\n';
    marker.close();
    chmod((mount.directory / ".btrfs-backup-session").c_str(), 0600);
}

OpenedBrowseRoot SystemBrowseSessionBackend::open(
    const ProfileId& profile_id, const BrowseSessionId& session_id, std::uint32_t caller_uid
) {
    require_root_directory(session_root_);
    const auto loaded = profiles_.get(profile_id);
    (void)acquire_target(loaded.profile);
    const std::string key{loaded.profile.target.luks_uuid.value()};
    const fs::path directory = session_root_ / std::string(session_id.value());
    const fs::path view = directory / "repository";
    try {
        if (fs::exists(directory))
            target_error("browse session directory already exists");
        fs::create_directories(view);
        chmod(directory.c_str(), 0711);
        chmod(view.c_str(), 0555);
        mount_read_only(loaded.profile.paths.remote_root.value(), view);
        const auto mounted = btrfsbackup::backup::mount_at(mounts_.inspect(), view);
        if (!mounted || !has_option(mounted->options, "ro")) {
            unmount(view);
            target_error("browse mount did not become read-only");
        }
        SessionMount session{key, directory, view};
        write_marker(session_id, profile_id, caller_uid, session);
        sessions_.emplace(std::string(session_id.value()), session);
        return {view};
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        release_target(key);
        throw;
    }
}

void SystemBrowseSessionBackend::release_target(const std::string& target_key) {
    auto target = targets_.find(target_key);
    if (target == targets_.end())
        return;
    if (--target->second->users != 0)
        return;
    if (target->second->mounted_by_backend && target->second->lock.try_upgrade_to_exclusive()) {
        const auto& profile = target->second->profile;
        (void)units_.stop_unit({btrfsbackup::config::target_mount_unit_name(profile.target.mount_point), std::chrono::minutes(2)});
    }
    targets_.erase(target);
}

void SystemBrowseSessionBackend::close(const BrowseSessionId& session_id) {
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        return;
    const SessionMount record = session->second;
    unmount(record.view);
    std::error_code error;
    fs::remove_all(record.directory, error);
    sessions_.erase(session);
    release_target(record.target_key);
}

void SystemBrowseSessionBackend::cleanup_stale() {
    if (!fs::exists(session_root_))
        return;
    require_root_directory(session_root_);
    for (const auto& entry : fs::directory_iterator(session_root_)) {
        if (entry.is_symlink() || !entry.is_directory())
            continue;
        const fs::path marker = entry.path() / ".btrfs-backup-session";
        struct stat status{};
        if (lstat(marker.c_str(), &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != 0)
            continue;
        const fs::path view = entry.path() / "repository";
        if (btrfsbackup::backup::mount_at(mounts_.inspect(), view).has_value())
            unmount(view);
        std::error_code ignored;
        fs::remove_all(entry.path(), ignored);
    }
}

} // namespace btrfsbackup::daemon::control
