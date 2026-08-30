// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseSessionService.hpp>

#include <array>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {
namespace {

BrowseSessionId random_session_id() {
    std::array<unsigned char, 16> bytes{};
    std::random_device random;
    for (auto& byte : bytes)
        byte = static_cast<unsigned char>(random());
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
    BrowseSessionClock clock,
    BrowseSessionEventSink events
) : authorizer_(authorizer), backend_(backend), lifetime_(lifetime),
    session_ids_(session_ids ? std::move(session_ids) : BrowseSessionIdGenerator{random_session_id}),
    clock_(clock ? std::move(clock) : BrowseSessionClock{[] { return std::chrono::system_clock::now(); }}),
    events_(std::move(events)) {
    if (lifetime_ <= std::chrono::seconds::zero())
        throw std::invalid_argument("browse session lifetime must be positive");
    backend_.cleanup_stale();
}

BrowseSessionService::~BrowseSessionService() noexcept {
    while (!sessions_.empty())
        close_noexcept(sessions_.begin(), BrowseSessionCloseReason::Shutdown);
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

    BrowseSessionId id = session_ids_();
    if (sessions_.contains(std::string(id.value())))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "browse session identifier collision");
    const auto expires_at = clock_() + lifetime_;
    OpenedBrowseRoot root = backend_.open(profile, id, caller_uid);
    const std::string key{id.value()};
    sessions_.emplace(key, Session{id, profile, caller_bus_name, caller_uid, expires_at});
    return {key, profile_id, root.path.string(), iso8601(expires_at), true};
}

void BrowseSessionService::close(const std::string& caller_bus_name, const std::string& session_id) {
    const BrowseSessionId validated{session_id};
    auto session = sessions_.find(std::string(validated.value()));
    if (session == sessions_.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "browse session was not found");
    if (session->second.caller_bus_name != caller_bus_name)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "browse session belongs to another caller");
    close_session(session, BrowseSessionCloseReason::Requested);
}

void BrowseSessionService::close_session(std::map<std::string, Session>::iterator session, BrowseSessionCloseReason reason) {
    const Session record = session->second;
    backend_.close(record.id);
    sessions_.erase(session);
    if (events_)
        events_({record.caller_uid, std::string(record.profile_id.value()), std::string(record.id.value()), reason, true});
}

void BrowseSessionService::close_noexcept(
    std::map<std::string, Session>::iterator session, BrowseSessionCloseReason reason
) noexcept {
    const Session record = session->second;
    bool succeeded = true;
    try { backend_.close(record.id); } catch (...) { succeeded = false; }
    sessions_.erase(session);
    if (events_) {
        try { events_({record.caller_uid, std::string(record.profile_id.value()), std::string(record.id.value()), reason, succeeded}); }
        catch (...) {}
    }
}

void BrowseSessionService::close_for_caller(const std::string& caller_bus_name) noexcept {
    for (auto session = sessions_.begin(); session != sessions_.end();) {
        if (session->second.caller_bus_name != caller_bus_name) { ++session; continue; }
        auto current = session++;
        close_noexcept(current, BrowseSessionCloseReason::CallerDisconnected);
    }
}

void BrowseSessionService::expire() noexcept {
    const auto now = clock_();
    for (auto session = sessions_.begin(); session != sessions_.end();) {
        if (session->second.expires_at > now) { ++session; continue; }
        auto current = session++;
        close_noexcept(current, BrowseSessionCloseReason::Expired);
    }
}

} // namespace btrfsbackup::daemon::control
