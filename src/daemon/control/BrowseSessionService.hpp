// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <core/Identifiers.hpp>
#include <daemon/ManagerResponseModels.hpp>
#include <daemon/control/OperationalControlService.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::daemon::control {

struct BrowseEntryInfo {
    std::string name;
    bool directory = false;
    std::uint64_t size = 0;
    std::uint32_t mode = 0;
    std::int64_t modified_at = 0;
};

struct BrowseDirectoryPage {
    std::vector<BrowseEntryInfo> entries;
    std::string continuation_token;
};

struct PreviousVersionInfo {
    std::string snapshot_id;
    std::string created_at;
    BrowseEntryInfo entry;
};

struct PreviousVersionsPage {
    std::vector<PreviousVersionInfo> entries;
    std::size_t next_offset = 0;
    bool has_more = false;
    std::string continuation_token;
};

struct BrowseAccessIdentity {
    std::uint32_t uid = 0;
    std::vector<std::uint32_t> groups;
};

class IBrowseSessionBackend {
  public:
    virtual ~IBrowseSessionBackend() = default;
    virtual void open(
        const ProfileId& profile_id,
        const BrowseSessionId& session_id,
        const BrowseAccessIdentity& caller_identity
    ) = 0;
    virtual void close(const BrowseSessionId& session_id) = 0;
    virtual void cleanup_stale() = 0;
    [[nodiscard]] virtual std::vector<BrowseEntryInfo> list_directory(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path,
        std::size_t maximum_entries
    ) = 0;
    [[nodiscard]] virtual BrowseDirectoryPage list_directory_page(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path,
        const std::string& after_name,
        std::size_t maximum_entries
    ) = 0;
    [[nodiscard]] virtual PreviousVersionsPage list_previous_versions(
        const BrowseSessionId& session_id,
        const std::string& profile_id,
        const std::string& source_id,
        const std::filesystem::path& relative_path,
        std::size_t offset,
        std::size_t maximum_entries
    ) = 0;
    [[nodiscard]] virtual BrowseEntryInfo inspect_entry(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path
    ) = 0;
    [[nodiscard]] virtual btrfsbackup::platform::linux::OwnedFileDescriptor open_file(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path
    ) = 0;
    [[nodiscard]] virtual btrfsbackup::platform::linux::OwnedFileDescriptor open_entry(
        const BrowseSessionId& session_id,
        const std::filesystem::path& relative_path
    ) = 0;
    [[nodiscard]] virtual std::string inspect_repository(const BrowseSessionId& session_id) = 0;
    [[nodiscard]] virtual std::vector<BackupCoverage> resolve_coverage(
        const std::filesystem::path& local_path,
        const std::vector<ProfileId>& profiles
    ) = 0;
};

enum class BrowseSessionCloseReason { Requested,
                                      TargetEject,
                                      CallerDisconnected,
                                      Expired,
                                      Shutdown };

struct BrowseSessionEvent {
    std::uint32_t caller_uid;
    std::string profile_id;
    std::string session_id;
    BrowseSessionCloseReason reason;
    bool succeeded;
};

using BrowseSessionSteadyClock = std::function<std::chrono::steady_clock::time_point()>;
using BrowseSessionWallClock = std::function<std::chrono::system_clock::time_point()>;
using BrowseSessionIdGenerator = std::function<BrowseSessionId()>;
using BrowseOperationLeaseIdGenerator = std::function<std::string()>;
using BrowseSessionEventSink = std::function<void(const BrowseSessionEvent&)>;

class BrowseSessionService final {
  public:
    BrowseSessionService(
        IManagerAuthorizer& authorizer,
        IBrowseSessionBackend& backend,
        std::chrono::seconds lifetime = std::chrono::minutes(15),
        BrowseSessionIdGenerator session_ids = {},
        BrowseSessionSteadyClock steady_clock = {},
        BrowseSessionWallClock wall_clock = {},
        BrowseSessionEventSink events = {},
        std::size_t global_limit = 64,
        std::size_t per_caller_limit = 8,
        std::size_t operation_lease_limit = 64,
        BrowseOperationLeaseIdGenerator operation_lease_ids = {},
        std::chrono::seconds maximum_lifetime = std::chrono::hours(1),
        std::chrono::seconds operation_lease_lifetime = std::chrono::minutes(5)
    );
    [[nodiscard]] BrowseSessionInfo renew(
        const std::string& caller_bus_name,
        const std::string& session_id
    );
    [[nodiscard]] std::string begin_operation(
        const std::string& caller_bus_name,
        const std::string& session_id
    );
    void end_operation(
        const std::string& caller_bus_name,
        const std::string& session_id,
        const std::string& lease_id
    );
    ~BrowseSessionService() noexcept;

    [[nodiscard]] BrowseSessionInfo open(
        const std::string& caller_bus_name,
        const BrowseAccessIdentity& caller_identity,
        const std::string& profile_id
    );
    [[nodiscard]] BrowseSessionInfo open(
        const std::string& caller_bus_name,
        std::uint32_t caller_uid,
        const std::string& profile_id
    );
    void close(const std::string& caller_bus_name, const std::string& session_id);
    [[nodiscard]] std::vector<BrowseEntryInfo> list_directory(
        const std::string& caller_bus_name,
        const std::string& session_id,
        const std::string& relative_path,
        std::size_t maximum_entries = 10000
    );
    [[nodiscard]] BrowseDirectoryPage list_directory_page(
        const std::string& caller_bus_name,
        const std::string& session_id,
        const std::string& relative_path,
        const std::string& continuation_token,
        std::size_t requested_entries
    );
    [[nodiscard]] BrowseEntryInfo inspect_entry(
        const std::string& caller_bus_name,
        const std::string& session_id,
        const std::string& relative_path
    );
    [[nodiscard]] PreviousVersionsPage list_previous_versions(
        const std::string& caller_bus_name,
        const std::string& session_id,
        const std::string& profile_id,
        const std::string& source_id,
        const std::string& relative_path,
        const std::string& continuation_token,
        std::size_t requested_entries
    );
    [[nodiscard]] btrfsbackup::platform::linux::OwnedFileDescriptor open_file(
        const std::string& caller_bus_name,
        const std::string& session_id,
        const std::string& relative_path
    );
    [[nodiscard]] btrfsbackup::platform::linux::OwnedFileDescriptor open_entry(
        const std::string& caller_bus_name,
        const std::string& session_id,
        const std::string& relative_path
    );
    [[nodiscard]] std::string inspect_repository(
        const std::string& caller_bus_name,
        const std::string& session_id
    );
    void close_for_caller(const std::string& caller_bus_name) noexcept;
    void begin_target_eject(const ProfileId& profile_id);
    void end_target_eject(const ProfileId& profile_id) noexcept;
    void expire() noexcept;
    [[nodiscard]] std::vector<BackupCoverage> resolve_coverage(
        const std::string& caller_bus_name,
        const std::string& local_path,
        const std::vector<ProfileId>& profiles
    );

  private:
    struct Session {
        BrowseSessionId id;
        ProfileId profile_id;
        std::string caller_bus_name;
        std::uint32_t caller_uid;
        std::chrono::steady_clock::time_point deadline;
        std::chrono::steady_clock::time_point absolute_deadline;
        std::chrono::system_clock::time_point expires_at;
        std::chrono::system_clock::time_point absolute_expires_at;
        std::map<std::string, std::chrono::steady_clock::time_point> operation_leases;
    };

    [[nodiscard]] std::map<std::string, Session>::iterator owned_session(
        const std::string& caller_bus_name,
        const std::string& session_id
    );
    [[nodiscard]] BrowseSessionInfo session_info(const Session& session) const;
    void extend(Session& session);
    void prune_expired_leases(Session& session, std::chrono::steady_clock::time_point now);
    void close_session(std::map<std::string, Session>::iterator session, BrowseSessionCloseReason reason);
    void close_noexcept(std::map<std::string, Session>::iterator session, BrowseSessionCloseReason reason) noexcept;

    IManagerAuthorizer& authorizer_;
    IBrowseSessionBackend& backend_;
    std::chrono::seconds lifetime_;
    BrowseSessionIdGenerator session_ids_;
    BrowseSessionSteadyClock steady_clock_;
    BrowseSessionWallClock wall_clock_;
    BrowseSessionEventSink events_;
    std::size_t global_limit_;
    std::size_t per_caller_limit_;
    std::size_t operation_lease_limit_;
    BrowseOperationLeaseIdGenerator operation_lease_ids_;
    std::chrono::seconds maximum_lifetime_;
    std::chrono::seconds operation_lease_lifetime_;
    std::map<std::string, Session> sessions_;
    std::set<ProfileId> ejecting_profiles_;
    std::map<ProfileId, std::chrono::steady_clock::time_point> browse_reopen_after_;
};

} // namespace btrfsbackup::daemon::control
