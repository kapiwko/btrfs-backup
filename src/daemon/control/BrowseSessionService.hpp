// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <core/Identifiers.hpp>
#include <daemon/ManagerResponseModels.hpp>
#include <daemon/control/OperationalControlService.hpp>

namespace btrfsbackup::daemon::control {

struct OpenedBrowseRoot { std::filesystem::path path; };

class IBrowseSessionBackend {
  public:
    virtual ~IBrowseSessionBackend() = default;
    [[nodiscard]] virtual OpenedBrowseRoot open(
        const ProfileId& profile_id, const BrowseSessionId& session_id, std::uint32_t caller_uid
    ) = 0;
    virtual void close(const BrowseSessionId& session_id) = 0;
    virtual void cleanup_stale() = 0;
    [[nodiscard]] virtual std::vector<BackupCoverage> resolve_coverage(
        const std::filesystem::path& local_path, const std::vector<ProfileId>& profiles
    ) = 0;
};

enum class BrowseSessionCloseReason { Requested, CallerDisconnected, Expired, Shutdown };

struct BrowseSessionEvent {
    std::uint32_t caller_uid;
    std::string profile_id;
    std::string session_id;
    BrowseSessionCloseReason reason;
    bool succeeded;
};

using BrowseSessionClock = std::function<std::chrono::system_clock::time_point()>;
using BrowseSessionIdGenerator = std::function<BrowseSessionId()>;
using BrowseSessionEventSink = std::function<void(const BrowseSessionEvent&)>;

class BrowseSessionService final {
  public:
    BrowseSessionService(
        IManagerAuthorizer& authorizer,
        IBrowseSessionBackend& backend,
        std::chrono::seconds lifetime = std::chrono::minutes(15),
        BrowseSessionIdGenerator session_ids = {},
        BrowseSessionClock clock = {},
        BrowseSessionEventSink events = {}
    );
    ~BrowseSessionService() noexcept;

    [[nodiscard]] BrowseSessionInfo open(
        const std::string& caller_bus_name, std::uint32_t caller_uid, const std::string& profile_id
    );
    void close(const std::string& caller_bus_name, const std::string& session_id);
    void close_for_caller(const std::string& caller_bus_name) noexcept;
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
        std::chrono::system_clock::time_point expires_at;
    };

    void close_session(std::map<std::string, Session>::iterator session, BrowseSessionCloseReason reason);
    void close_noexcept(std::map<std::string, Session>::iterator session, BrowseSessionCloseReason reason) noexcept;

    IManagerAuthorizer& authorizer_;
    IBrowseSessionBackend& backend_;
    std::chrono::seconds lifetime_;
    BrowseSessionIdGenerator session_ids_;
    BrowseSessionClock clock_;
    BrowseSessionEventSink events_;
    std::map<std::string, Session> sessions_;
};

} // namespace btrfsbackup::daemon::control
