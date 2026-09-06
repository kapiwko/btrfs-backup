// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <backup/ports/IMountInspector.hpp>
#include <config/ports/IProfileRepository.hpp>
#include <daemon/control/BrowseSessionService.hpp>
#include <daemon/control/BrowseFilesystemAccess.hpp>
#include <daemon/control/BrowseSessionMountStore.hpp>
#include <daemon/control/SystemdUnitController.hpp>
#include <platform/linux/filesystem/FileLock.hpp>

namespace btrfsbackup::daemon::control {

class SystemBrowseSessionBackend final : public IBrowseSessionBackend {
  public:
    SystemBrowseSessionBackend(
        btrfsbackup::config::IProfileRepository& profiles,
        btrfsbackup::backup::IMountInspector& mounts,
        ISystemdUnitController& units,
        std::filesystem::path session_root = "/run/btrfs-backup-browse",
        std::filesystem::path lock_root = btrfsbackup::platform::linux::filesystem::default_lock_root()
    );
    ~SystemBrowseSessionBackend() noexcept override;

    void open(
        const ProfileId& profile_id,
        const BrowseSessionId& session_id,
        std::uint32_t caller_uid
    ) override;
    void close(const BrowseSessionId& session_id) override;
    void cleanup_stale() override;
    [[nodiscard]] std::vector<BrowseEntryInfo> list_directory(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path,
        std::size_t maximum_entries
    ) override;
    [[nodiscard]] BrowseDirectoryPage list_directory_page(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path,
        const std::string& after_name,
        std::size_t maximum_entries
    ) override;
    [[nodiscard]] PreviousVersionsPage list_previous_versions(
        const BrowseSessionId& session_id,
        const std::string& profile_id,
        const std::string& source_id,
        const std::filesystem::path& relative_path,
        std::size_t offset,
        std::size_t maximum_entries
    ) override;
    [[nodiscard]] BrowseEntryInfo inspect_entry(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path
    ) override;
    [[nodiscard]] btrfsbackup::platform::linux::OwnedFileDescriptor open_file(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path
    ) override;
    [[nodiscard]] btrfsbackup::platform::linux::OwnedFileDescriptor open_entry(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path
    ) override;
    [[nodiscard]] std::string inspect_repository(const BrowseSessionId& session_id) override;
    [[nodiscard]] std::vector<BackupCoverage> resolve_coverage(
        const std::filesystem::path& local_path,
        const std::vector<ProfileId>& profiles
    ) override;

  private:
    struct CachedSnapshot {
        std::string snapshot_id;
        std::string profile_id;
        std::string source_id;
        std::filesystem::path repository_path;
        std::string created_at;
        bool verified = false;
    };
    struct PreviousVersionsCacheEntry {
        std::vector<PreviousVersionInfo> entries;
    };
    struct TargetLease {
        btrfsbackup::config::Profile profile;
        btrfsbackup::platform::linux::filesystem::FileLock lock;
        std::size_t users = 0;
        bool mounted_by_backend = false;
    };
    struct SessionMount {
        BrowseSessionMountRecord mount;
        BrowseAccessIdentity identity;
        bool repository_cached = false;
        std::string repository_document;
        std::vector<CachedSnapshot> snapshots;
        std::map<std::string, PreviousVersionsCacheEntry> previous_versions;
    };

    TargetLease& acquire_target(const btrfsbackup::config::Profile& profile);
    void release_target(const std::string& target_key);
    void mount_read_only(
        const BrowseSessionId& session_id,
        const std::filesystem::path& source,
        const std::filesystem::path& target
    );
    void unmount(const BrowseSessionId& session_id, const std::filesystem::path& target);
    void cleanup_record(const BrowseSessionId& session_id, SessionMount& mount, bool release_live_target);
    void ensure_repository_cache(SessionMount& mount);
    [[nodiscard]] std::pair<std::filesystem::path, std::filesystem::path> authorized_snapshot_path(
        SessionMount& mount,
        const std::filesystem::path& relative_path
    );

    btrfsbackup::config::IProfileRepository& profiles_;
    btrfsbackup::backup::IMountInspector& mounts_;
    ISystemdUnitController& units_;
    BrowseFilesystemAccess filesystem_access_;
    BrowseSessionMountStore mount_store_;
    std::filesystem::path lock_root_;
    std::map<std::string, std::unique_ptr<TargetLease>> targets_;
    std::map<std::string, SessionMount> sessions_;
};

} // namespace btrfsbackup::daemon::control
