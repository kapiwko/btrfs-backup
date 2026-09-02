// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseSessionService.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <ctime>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <sys/random.h>
#include <utility>

#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {
namespace {

BrowseSessionId random_session_id() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        throw std::runtime_error("cannot generate browse session identifier");
    }
    std::ostringstream value;
    value << "browse-" << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        value << std::setw(2) << static_cast<unsigned>(byte);
    return BrowseSessionId{value.str()};
}

std::string iso8601(std::chrono::system_clock::time_point value) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(value);
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

} // namespace

BrowseSessionService::BrowseSessionService(
    IManagerAuthorizer& authorizer,
    IBrowseSessionBackend& backend,
    std::chrono::seconds lifetime,
    BrowseSessionIdGenerator session_ids,
    BrowseSessionSteadyClock steady_clock,
    BrowseSessionWallClock wall_clock,
    BrowseSessionEventSink events,
    std::size_t global_limit,
    std::size_t per_uid_limit
) : authorizer_(authorizer), backend_(backend), lifetime_(lifetime),
    session_ids_(session_ids ? std::move(session_ids) : BrowseSessionIdGenerator{random_session_id}),
    steady_clock_(steady_clock ? std::move(steady_clock) : BrowseSessionSteadyClock{[] { return std::chrono::steady_clock::now(); }}),
    wall_clock_(wall_clock ? std::move(wall_clock) : BrowseSessionWallClock{[] { return std::chrono::system_clock::now(); }}),
    events_(std::move(events)), global_limit_(global_limit), per_uid_limit_(per_uid_limit) {
    if (lifetime_ <= std::chrono::seconds::zero())
        throw std::invalid_argument("browse session lifetime must be positive");
    if (global_limit_ == 0 || per_uid_limit_ == 0 || per_uid_limit_ > global_limit_)
        throw std::invalid_argument("browse session limits are invalid");
    backend_.cleanup_stale();
}

BrowseSessionService::~BrowseSessionService() noexcept {
    for (auto session = sessions_.begin(); session != sessions_.end();) {
        auto current = session++;
        close_noexcept(current, BrowseSessionCloseReason::Shutdown);
    }
}

BrowseSessionInfo BrowseSessionService::open(
    const std::string& caller_bus_name,
    std::uint32_t caller_uid,
    const std::string& profile_id
) {
    const ProfileId profile{profile_id};
    if (caller_bus_name.empty() || !authorizer_.authorize(caller_bus_name, ManagerAuthorizationAction::OpenBrowseSession) ||
        !authorizer_.caller_is_active(caller_bus_name))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "browse session is not authorized");
    if (sessions_.size() >= global_limit_)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "browse session limit reached");
    const auto caller_sessions = std::ranges::count_if(sessions_, [&](const auto& item) {
        return item.second.caller_uid == caller_uid;
    });
    if (static_cast<std::size_t>(caller_sessions) >= per_uid_limit_)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "user browse session limit reached");

    BrowseSessionId id = session_ids_();
    if (sessions_.contains(std::string(id.value())))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "browse session identifier collision");
    OpenedBrowseRoot root = backend_.open(profile, id, caller_uid);
    const std::string key{id.value()};
    Session session{id, profile, caller_bus_name, caller_uid, root.path, {}, {}};
    extend(session);
    auto [position, inserted] = sessions_.emplace(key, std::move(session));
    (void)inserted;
    return session_info(position->second);
}

std::map<std::string, BrowseSessionService::Session>::iterator BrowseSessionService::owned_session(
    const std::string& caller_bus_name,
    const std::string& session_id
) {
    const BrowseSessionId validated{session_id};
    auto session = sessions_.find(std::string(validated.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    if (session->second.caller_bus_name != caller_bus_name)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "browse session belongs to another caller");
    return session;
}

void BrowseSessionService::extend(Session& session) {
    session.deadline = steady_clock_() + lifetime_;
    session.expires_at = wall_clock_() + lifetime_;
}

BrowseSessionInfo BrowseSessionService::session_info(const Session& session) const {
    return {
        std::string(session.id.value()),
        std::string(session.profile_id.value()),
        session.root_path.string(),
        iso8601(session.expires_at),
        true,
    };
}

BrowseSessionInfo BrowseSessionService::renew(
    const std::string& caller_bus_name,
    const std::string& session_id
) {
    auto session = owned_session(caller_bus_name, session_id);
    extend(session->second);
    return session_info(session->second);
}

void BrowseSessionService::set_active(
    const std::string& caller_bus_name,
    const std::string& session_id,
    bool active
) {
    auto session = owned_session(caller_bus_name, session_id);
    session->second.active = active;
    extend(session->second);
}

void BrowseSessionService::close(const std::string& caller_bus_name, const std::string& session_id) {
    close_session(owned_session(caller_bus_name, session_id), BrowseSessionCloseReason::Requested);
}

std::vector<BrowseEntryInfo> BrowseSessionService::list_directory(
    const std::string& caller_bus_name,
    const std::string& session_id,
    const std::string& relative_path,
    std::size_t maximum_entries
) {
    auto session = owned_session(caller_bus_name, session_id);
    extend(session->second);
    return backend_.list_directory(session->second.id, relative_path, maximum_entries);
}

BrowseEntryInfo BrowseSessionService::inspect_entry(
    const std::string& caller_bus_name,
    const std::string& session_id,
    const std::string& relative_path
) {
    auto session = owned_session(caller_bus_name, session_id);
    extend(session->second);
    return backend_.inspect_entry(session->second.id, relative_path);
}

btrfsbackup::platform::linux::OwnedFileDescriptor BrowseSessionService::open_file(
    const std::string& caller_bus_name,
    const std::string& session_id,
    const std::string& relative_path
) {
    auto session = owned_session(caller_bus_name, session_id);
    extend(session->second);
    return backend_.open_file(session->second.id, relative_path);
}

void BrowseSessionService::close_session(std::map<std::string, Session>::iterator session, BrowseSessionCloseReason reason) {
    session->second.active = false;
    session->second.deadline = std::chrono::steady_clock::time_point::min();
    const Session record = session->second;
    backend_.close(record.id);
    sessions_.erase(session);
    if (events_)
        events_({record.caller_uid, std::string(record.profile_id.value()), std::string(record.id.value()), reason, true});
}

void BrowseSessionService::close_noexcept(
    std::map<std::string, Session>::iterator session,
    BrowseSessionCloseReason reason
) noexcept {
    session->second.active = false;
    session->second.deadline = std::chrono::steady_clock::time_point::min();
    const Session record = session->second;
    bool succeeded = true;
    try {
        backend_.close(record.id);
    } catch (...) {
        succeeded = false;
    }
    if (succeeded)
        sessions_.erase(session);
    if (events_) {
        try {
            events_({record.caller_uid, std::string(record.profile_id.value()), std::string(record.id.value()), reason, succeeded});
        } catch (...) {}
    }
}

void BrowseSessionService::close_for_caller(const std::string& caller_bus_name) noexcept {
    for (auto session = sessions_.begin(); session != sessions_.end();) {
        if (session->second.caller_bus_name != caller_bus_name) {
            ++session;
            continue;
        }
        auto current = session++;
        close_noexcept(current, BrowseSessionCloseReason::CallerDisconnected);
    }
}

void BrowseSessionService::expire() noexcept {
    try {
        backend_.cleanup_stale();
    } catch (...) {}
    const auto now = steady_clock_();
    for (auto session = sessions_.begin(); session != sessions_.end();) {
        if (session->second.active || session->second.deadline > now) {
            ++session;
            continue;
        }
        auto current = session++;
        close_noexcept(current, BrowseSessionCloseReason::Expired);
    }
}

std::vector<BackupCoverage> BrowseSessionService::resolve_coverage(
    const std::string& caller_bus_name,
    const std::string& local_path,
    const std::vector<ProfileId>& profiles
) {
    const std::filesystem::path path{local_path};
    if (caller_bus_name.empty() || !path.is_absolute() ||
        !authorizer_.authorize(caller_bus_name, ManagerAuthorizationAction::OpenBrowseSession) ||
        !authorizer_.caller_is_active(caller_bus_name))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "backup coverage query is not authorized");
    return backend_.resolve_coverage(path.lexically_normal(), profiles);
}

} // namespace btrfsbackup::daemon::control
