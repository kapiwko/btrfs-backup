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
    [[nodiscard]] BrowseEntryInfo inspect_entry(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path
    ) override;
    [[nodiscard]] btrfsbackup::platform::linux::OwnedFileDescriptor open_file(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path
    ) override;
    [[nodiscard]] btrfsbackup::platform::linux::OwnedFileDescriptor open_root(
        const BrowseSessionId& session_id
    ) override;
    [[nodiscard]] std::string inspect_repository(const BrowseSessionId& session_id) override;
    [[nodiscard]] std::vector<BackupCoverage> resolve_coverage(
        const std::filesystem::path& local_path,
        const std::vector<ProfileId>& profiles
    ) override;

  private:
    struct TargetLease {
        btrfsbackup::config::Profile profile;
        btrfsbackup::platform::linux::filesystem::FileLock lock;
        std::size_t users = 0;
        bool mounted_by_backend = false;
    };
    struct SessionMount {
        std::string target_key;
        std::string target_unit;
        std::filesystem::path directory;
        std::filesystem::path view;
        std::filesystem::path marker;
        std::uint32_t caller_uid = 0;
        bool view_mounted = false;
        bool target_mounted_by_backend = false;
        bool target_released = false;
    };

    TargetLease& acquire_target(const btrfsbackup::config::Profile& profile);
    void release_target(const std::string& target_key);
    void mount_read_only(
        const BrowseSessionId& session_id,
        const std::filesystem::path& source,
        const std::filesystem::path& target
    );
    void unmount(const BrowseSessionId& session_id, const std::filesystem::path& target);
    void write_marker(const BrowseSessionId& id, const SessionMount& mount);
    [[nodiscard]] std::optional<SessionMount> read_marker(const std::filesystem::path& marker) const;
    void cleanup_record(const BrowseSessionId& session_id, SessionMount& mount, bool release_live_target);

    btrfsbackup::config::IProfileRepository& profiles_;
    btrfsbackup::backup::IMountInspector& mounts_;
    ISystemdUnitController& units_;
    std::filesystem::path session_root_;
    std::filesystem::path lock_root_;
    std::map<std::string, std::unique_ptr<TargetLease>> targets_;
    std::map<std::string, SessionMount> sessions_;
};

} // namespace btrfsbackup::daemon::control
