// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemBrowseSessionBackend.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <backup/planning/BackupPreflightValidation.hpp>
#include <config/ProfileRender.hpp>
#include <config/json/JsonIo.hpp>
#include <config/domain/Validation.hpp>
#include <core/RuntimeTime.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/storage/LibBtrfsOperations.hpp>
#include <restore/RepositoryDiscoveryService.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using btrfsbackup::platform::linux::OwnedFileDescriptor;

[[noreturn]] void target_error(const std::string& message) {
    throw dbus::ManagerOperationError(dbus::ManagerErrorCode::TargetUnavailable, message);
}

} // namespace

SystemBrowseSessionBackend::SystemBrowseSessionBackend(
    btrfsbackup::config::IProfileRepository& profiles,
    btrfsbackup::backup::IMountInspector& mounts,
    ISystemdUnitController& units,
    fs::path session_root,
    fs::path lock_root
) : profiles_(profiles), mounts_(mounts), units_(units), mount_store_(std::move(session_root)), lock_root_(std::move(lock_root)) {
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
            .unit = btrfsbackup::config::target_mount_unit_name(profile.target.mount_point),
            .timeout = std::chrono::minutes(2),
            .runtime_properties = {},
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

void SystemBrowseSessionBackend::open(
    const ProfileId& profile_id,
    const BrowseSessionId& session_id,
    const BrowseAccessIdentity& caller_identity
) {
    mount_store_.prepare_root();
    const auto loaded = profiles_.get(profile_id);
    const TargetLease& target = acquire_target(loaded.profile);
    const std::string key{loaded.profile.target.luks_uuid.value()};
    SessionMount record{
        .mount = mount_store_.make_record(
            session_id,
            caller_identity.uid,
            key,
            btrfsbackup::config::target_mount_unit_name(loaded.profile.target.mount_point),
            target.mounted_by_backend
        ),
        .identity = caller_identity,
        .repository_cached = false,
        .repository_document = {},
        .snapshots = {},
        .previous_versions = {},
    };
    auto [session, inserted] = sessions_.emplace(std::string(session_id.value()), record);
    if (!inserted) {
        release_target(key);
        target_error("browse session already exists");
    }
    try {
        if (fs::exists(session->second.mount.directory))
            target_error("browse session directory already exists");
        fs::create_directories(session->second.mount.view);
        if (chown(session->second.mount.directory.c_str(), 0, static_cast<gid_t>(-1)) != 0 ||
            chown(session->second.mount.view.c_str(), 0, static_cast<gid_t>(-1)) != 0 ||
            chmod(session->second.mount.directory.c_str(), 0700) != 0 || chmod(session->second.mount.view.c_str(), 0700) != 0)
            target_error("cannot secure browse session directory");
        mount_store_.write(session_id, session->second.mount);
        session->second.mount.view_mounted = true;
        mount_store_.write(session_id, session->second.mount);
        mount_read_only(session_id, loaded.profile.paths.remote_root.value(), session->second.mount.view);
        return;
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
    if (mount.mount.view_mounted) {
        unmount(session_id, mount.mount.view);
        mount.mount.view_mounted = false;
        mount_store_.write(session_id, mount.mount);
    }
    mount_store_.remove_session_directory(mount.mount);
    if (!mount.mount.target_released) {
        if (release_live_target) {
            release_target(mount.mount.target_key);
        } else if (mount.mount.target_mounted_by_backend) {
            if (targets_.contains(mount.mount.target_key))
                target_error("stale target cleanup is deferred while the target is in use");
            const auto result = units_.stop_unit({mount.mount.target_unit, std::chrono::minutes(2)});
            if (!result)
                target_error("cannot unmount backup target after stale browse session");
        }
        mount.mount.target_released = true;
        mount_store_.write(session_id, mount.mount);
    }
    mount_store_.remove_marker(mount.mount);
}

void SystemBrowseSessionBackend::close(const BrowseSessionId& session_id) {
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        return;
    cleanup_record(session_id, session->second, true);
    sessions_.erase(session);
}

void SystemBrowseSessionBackend::cleanup_stale() {
    std::set<std::string> live_session_ids;
    for (const auto& [id, session] : sessions_) {
        (void)session;
        live_session_ids.insert(id);
    }
    auto stored_records = mount_store_.stale_records(live_session_ids);
    std::vector<std::pair<BrowseSessionId, SessionMount>> records;
    records.reserve(stored_records.size());
    for (auto& [id, record] : stored_records) {
        records.emplace_back(std::move(id), SessionMount{
                                                .mount = std::move(record),
                                                .identity = {},
                                                .repository_cached = false,
                                                .repository_document = {},
                                                .snapshots = {},
                                                .previous_versions = {},
                                            });
    }
    std::set<std::string> targets_with_mounted_views;
    for (auto& [id, record] : records) {
        if (record.mount.view_mounted) {
            try {
                unmount(id, record.mount.view);
                record.mount.view_mounted = false;
                mount_store_.write(id, record.mount);
            } catch (...) {}
        }
        if (record.mount.view_mounted)
            targets_with_mounted_views.insert(record.mount.target_key);
    }
    for (auto& [id, record] : records) {
        if (record.mount.view_mounted || targets_with_mounted_views.contains(record.mount.target_key))
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
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    const auto [root, path] = authorized_snapshot_path(session->second, relative_path);
    return filesystem_access_.list_directory(root, path, maximum_entries, &session->second.identity);
}

BrowseDirectoryPage SystemBrowseSessionBackend::list_directory_page(
    const BrowseSessionId& session_id,
    const fs::path& relative_path,
    const std::string& after_name,
    std::size_t maximum_entries
) {
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    const auto [root, path] = authorized_snapshot_path(session->second, relative_path);
    return filesystem_access_.list_directory_page(
        root,
        path,
        after_name,
        maximum_entries,
        &session->second.identity
    );
}

void SystemBrowseSessionBackend::ensure_repository_cache(SessionMount& mount) {
    if (mount.repository_cached)
        return;
    btrfsbackup::restore::RepositoryDiscoveryService discovery([](const fs::path& path) {
        const auto value = btrfsbackup::platform::linux::storage::read_btrfs_snapshot_metadata(path);
        if (!value)
            return std::optional<btrfsbackup::restore::DiscoveredSnapshotMetadata>{};
        return std::optional{btrfsbackup::restore::DiscoveredSnapshotMetadata{
            value->is_subvolume,
            value->readonly,
            value->uuid.value(),
            value->received_uuid.value(),
        }};
    });
    const auto catalog = discovery.discover(mount.mount.view);
    config::json::Json snapshots = config::json::Json::array();
    std::vector<CachedSnapshot> cached_snapshots;
    cached_snapshots.reserve(catalog.snapshots().size());
    for (const auto& snapshot : catalog.snapshots()) {
        const std::string created_at = format_utc_iso_timestamp(snapshot.created_at);
        snapshots.push_back({
            {"snapshotId", snapshot.snapshot_id},
            {"hostId", snapshot.host_id},
            {"profileId", snapshot.profile_id},
            {"sourceId", snapshot.source_id},
            {"relativePath", snapshot.repository_path.value().string()},
            {"createdAt", created_at},
            {"uuid", snapshot.uuid},
            {"receivedUuid", snapshot.received_uuid},
            {"parentUuid", snapshot.parent_uuid},
            {"verified", snapshot.verified},
        });
        cached_snapshots.push_back({
            snapshot.snapshot_id,
            snapshot.profile_id,
            snapshot.source_id,
            snapshot.repository_path.value(),
            created_at,
            snapshot.verified,
        });
    }
    mount.repository_document = config::json::dump_json({
        {"schemaVersion", 1},
        {"repositoryId", catalog.identity().repository_id},
        {"targetFilesystemUuid", catalog.identity().target_filesystem_uuid},
        {"createdAt", format_utc_iso_timestamp(catalog.identity().created_at)},
        {"features", catalog.identity().features},
        {"generation", catalog.generation()},
        {"snapshots", std::move(snapshots)},
    });
    mount.snapshots = std::move(cached_snapshots);
    mount.repository_cached = true;
}

std::pair<fs::path, fs::path> SystemBrowseSessionBackend::authorized_snapshot_path(
    SessionMount& mount,
    const fs::path& relative_path
) {
    ensure_repository_cache(mount);
    const fs::path normalized = BrowseFilesystemAccess::normalize_relative_path(relative_path);
    const CachedSnapshot* selected = nullptr;
    for (const CachedSnapshot& snapshot : mount.snapshots) {
        if (!snapshot.verified ||
            !btrfsbackup::config::path_is_within(normalized, snapshot.repository_path))
            continue;
        if (selected == nullptr || snapshot.repository_path.native().size() > selected->repository_path.native().size())
            selected = &snapshot;
    }
    if (selected == nullptr)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotAuthorized,
            "browse path is outside a verified snapshot"
        );
    fs::path within = normalized.lexically_relative(selected->repository_path);
    if (within.empty())
        within = ".";
    return {mount.mount.view / selected->repository_path, within};
}

PreviousVersionsPage SystemBrowseSessionBackend::list_previous_versions(
    const BrowseSessionId& session_id,
    const std::string& profile_id,
    const std::string& source_id,
    const fs::path& relative_path,
    std::size_t offset,
    std::size_t maximum_entries
) {
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");

    const fs::path normalized_path = BrowseFilesystemAccess::normalize_relative_path(relative_path);
    const std::string cache_key = profile_id + '\0' + source_id + '\0' + normalized_path.generic_string();
    ensure_repository_cache(session->second);
    auto cached = session->second.previous_versions.find(cache_key);
    if (cached == session->second.previous_versions.end()) {
        std::vector<CachedSnapshot> candidates;
        for (const CachedSnapshot& snapshot : session->second.snapshots) {
            if (snapshot.verified && snapshot.profile_id == profile_id && snapshot.source_id == source_id)
                candidates.push_back(snapshot);
        }
        std::ranges::sort(candidates, [](const CachedSnapshot& left, const CachedSnapshot& right) {
            if (left.created_at != right.created_at)
                return left.created_at > right.created_at;
            return left.snapshot_id < right.snapshot_id;
        });

        PreviousVersionsCacheEntry result;
        result.entries.reserve(candidates.size());
        for (const CachedSnapshot& snapshot : candidates) {
            fs::path entry_path = snapshot.repository_path;
            if (normalized_path != ".")
                entry_path /= normalized_path;
            try {
                const auto [root, path] = authorized_snapshot_path(session->second, entry_path);
                BrowseEntryInfo entry = filesystem_access_.inspect_entry(root, path, &session->second.identity);
                result.entries.push_back({
                    snapshot.snapshot_id,
                    snapshot.created_at,
                    std::move(entry),
                });
            } catch (const std::system_error& error) {
                if (error.code().value() != ENOENT && error.code().value() != ENOTDIR)
                    throw;
            } catch (const std::invalid_argument&) {
                continue;
            }
        }
        cached = session->second.previous_versions.emplace(cache_key, std::move(result)).first;
    }

    if (offset > cached->second.entries.size())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "previous-versions continuation is out of range");
    const std::size_t remaining = cached->second.entries.size() - offset;
    const std::size_t end = offset + std::min(remaining, maximum_entries);
    PreviousVersionsPage page;
    page.entries.insert(
        page.entries.end(),
        cached->second.entries.begin() + static_cast<std::ptrdiff_t>(offset),
        cached->second.entries.begin() + static_cast<std::ptrdiff_t>(end)
    );
    page.next_offset = end;
    page.has_more = end < cached->second.entries.size();
    return page;
}

BrowseEntryInfo SystemBrowseSessionBackend::inspect_entry(
    const BrowseSessionId& session_id,
    const fs::path& relative_path
) {
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    const auto [root, path] = authorized_snapshot_path(session->second, relative_path);
    return filesystem_access_.inspect_entry(root, path, &session->second.identity);
}

OwnedFileDescriptor SystemBrowseSessionBackend::open_file(
    const BrowseSessionId& session_id,
    const fs::path& relative_path
) {
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    const auto [root, path] = authorized_snapshot_path(session->second, relative_path);
    return filesystem_access_.open_file(root, path, &session->second.identity);
}

OwnedFileDescriptor SystemBrowseSessionBackend::open_entry(
    const BrowseSessionId& session_id,
    const fs::path& relative_path
) {
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    const auto [root, path] = authorized_snapshot_path(session->second, relative_path);
    return filesystem_access_.open_entry(root, path, &session->second.identity);
}

std::string SystemBrowseSessionBackend::inspect_repository(const BrowseSessionId& session_id) {
    auto session = sessions_.find(std::string(session_id.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    ensure_repository_cache(session->second);
    return session->second.repository_document;
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
