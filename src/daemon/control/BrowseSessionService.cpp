// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseSessionService.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
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

constexpr std::size_t maximum_browse_page_entries = 512;
constexpr std::size_t maximum_browse_token_size = 32768;
constexpr std::chrono::seconds browse_reopen_delay{2};

char hex_digit(unsigned int value) {
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

std::string hex_encode(std::string_view value) {
    std::string result;
    result.reserve(value.size() * 2);
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(hex_digit(byte >> 4));
        result.push_back(hex_digit(byte & 0x0f));
    }
    return result;
}

int hex_value(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

std::string page_binding(const BrowseSessionId& session_id, const std::string& relative_path) {
    return std::string(session_id.value()) + '\0' +
        std::filesystem::path(relative_path).lexically_normal().generic_string() + '\0';
}

std::string previous_versions_binding(
    const BrowseSessionId& session_id,
    const std::string& profile_id,
    const std::string& source_id,
    const std::string& relative_path
) {
    std::string normalized_path = std::filesystem::path(relative_path).lexically_normal().generic_string();
    if (normalized_path.empty())
        normalized_path = ".";
    return std::string(session_id.value()) + '\0' + profile_id + '\0' + source_id + '\0' +
        normalized_path + '\0';
}

std::string encode_bound_token(std::string_view binding, std::string_view cursor) {
    return "v1:" + hex_encode(std::string(binding) + std::string(cursor));
}

std::string decode_bound_token(std::string_view binding, const std::string& token) {
    if (token.empty())
        return {};
    if (!token.starts_with("v1:") || token.size() > maximum_browse_token_size || (token.size() - 3) % 2 != 0)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "invalid browse continuation token");
    std::string decoded;
    decoded.reserve((token.size() - 3) / 2);
    for (std::size_t index = 3; index < token.size(); index += 2) {
        const int high = hex_value(token[index]);
        const int low = hex_value(token[index + 1]);
        if (high < 0 || low < 0)
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "invalid browse continuation token");
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    if (!decoded.starts_with(binding))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "browse continuation token does not match the session and query");
    return decoded.substr(binding.size());
}

std::string make_continuation_token(
    const BrowseSessionId& session_id,
    const std::string& relative_path,
    const std::string& last_name
) {
    return encode_bound_token(page_binding(session_id, relative_path), last_name);
}

std::string continuation_name(
    const BrowseSessionId& session_id,
    const std::string& relative_path,
    const std::string& token
) {
    if (token.empty())
        return {};
    const std::string binding = page_binding(session_id, relative_path);
    const std::string name = decode_bound_token(binding, token);
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
        name.find('\0') != std::string::npos)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "invalid browse continuation token");
    return name;
}

std::size_t previous_versions_offset(
    const BrowseSessionId& session_id,
    const std::string& profile_id,
    const std::string& source_id,
    const std::string& relative_path,
    const std::string& token
) {
    if (token.empty())
        return 0;
    const std::string value = decode_bound_token(
        previous_versions_binding(session_id, profile_id, source_id, relative_path),
        token
    );
    std::size_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "invalid previous-versions continuation token");
    return result;
}

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

std::string random_operation_lease_id() {
    std::string value{random_session_id().value()};
    value.replace(0, std::string_view{"browse"}.size(), "lease");
    return value;
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
    std::size_t per_caller_limit,
    std::size_t operation_lease_limit,
    BrowseOperationLeaseIdGenerator operation_lease_ids
) : authorizer_(authorizer), backend_(backend), lifetime_(lifetime),
    session_ids_(session_ids ? std::move(session_ids) : BrowseSessionIdGenerator{random_session_id}),
    steady_clock_(steady_clock ? std::move(steady_clock) : BrowseSessionSteadyClock{[] { return std::chrono::steady_clock::now(); }}),
    wall_clock_(wall_clock ? std::move(wall_clock) : BrowseSessionWallClock{[] { return std::chrono::system_clock::now(); }}),
    events_(std::move(events)), global_limit_(global_limit), per_caller_limit_(per_caller_limit),
    operation_lease_limit_(operation_lease_limit),
    operation_lease_ids_(operation_lease_ids ? std::move(operation_lease_ids) : BrowseOperationLeaseIdGenerator{random_operation_lease_id}) {
    if (lifetime_ <= std::chrono::seconds::zero())
        throw std::invalid_argument("browse session lifetime must be positive");
    if (global_limit_ == 0 || per_caller_limit_ == 0 || per_caller_limit_ > global_limit_ || operation_lease_limit_ == 0)
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
    const BrowseAccessIdentity& caller_identity,
    const std::string& profile_id
) {
    const ProfileId profile{profile_id};
    if (caller_bus_name.empty() || !authorizer_.authorize(caller_bus_name, ManagerAuthorizationAction::OpenBrowseSession) ||
        !authorizer_.caller_is_active(caller_bus_name))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "browse session is not authorized");
    if (ejecting_profiles_.contains(profile))
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Busy,
            "backup target is being ejected"
        );
    if (const auto delayed = browse_reopen_after_.find(profile); delayed != browse_reopen_after_.end()) {
        if (steady_clock_() < delayed->second)
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Busy,
                "backup browsing is settling after target eject"
            );
        browse_reopen_after_.erase(delayed);
    }
    if (sessions_.size() >= global_limit_)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "browse session limit reached");
    const auto caller_sessions = std::ranges::count_if(sessions_, [&](const auto& item) {
        return item.second.caller_bus_name == caller_bus_name;
    });
    if (static_cast<std::size_t>(caller_sessions) >= per_caller_limit_)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "caller browse session limit reached");

    BrowseSessionId id = session_ids_();
    if (sessions_.contains(std::string(id.value())))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "browse session identifier collision");
    backend_.open(profile, id, caller_identity);
    const std::string key{id.value()};
    Session session{
        .id = id,
        .profile_id = profile,
        .caller_bus_name = caller_bus_name,
        .caller_uid = caller_identity.uid,
        .deadline = {},
        .expires_at = {},
        .operation_leases = {},
    };
    extend(session);
    auto [position, inserted] = sessions_.emplace(key, std::move(session));
    (void)inserted;
    return session_info(position->second);
}

BrowseSessionInfo BrowseSessionService::open(
    const std::string& caller_bus_name,
    std::uint32_t caller_uid,
    const std::string& profile_id
) {
    return open(caller_bus_name, BrowseAccessIdentity{.uid = caller_uid, .groups = {}}, profile_id);
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

std::string BrowseSessionService::begin_operation(
    const std::string& caller_bus_name,
    const std::string& session_id
) {
    auto session = owned_session(caller_bus_name, session_id);
    if (session->second.operation_leases.size() >= operation_lease_limit_)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "browse operation lease limit reached");
    std::string lease_id = operation_lease_ids_();
    if (lease_id.empty() || lease_id.size() > 128)
        throw std::runtime_error("browse operation lease generator returned an invalid identifier");
    if (!session->second.operation_leases.insert(lease_id).second)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "browse operation lease identifier collision");
    extend(session->second);
    return lease_id;
}

void BrowseSessionService::end_operation(
    const std::string& caller_bus_name,
    const std::string& session_id,
    const std::string& lease_id
) {
    auto session = owned_session(caller_bus_name, session_id);
    if (lease_id.empty() || lease_id.size() > 128 || session->second.operation_leases.erase(lease_id) != 1)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "browse operation lease is unknown");
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

BrowseDirectoryPage BrowseSessionService::list_directory_page(
    const std::string& caller_bus_name,
    const std::string& session_id,
    const std::string& relative_path,
    const std::string& continuation_token,
    std::size_t requested_entries
) {
    auto session = owned_session(caller_bus_name, session_id);
    if (requested_entries == 0 || requested_entries > maximum_browse_page_entries)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "browse page size must be between 1 and 512");
    const std::string after_name = continuation_name(session->second.id, relative_path, continuation_token);
    extend(session->second);
    BrowseDirectoryPage page = backend_.list_directory_page(
        session->second.id,
        relative_path,
        after_name,
        requested_entries
    );
    if (!page.continuation_token.empty())
        page.continuation_token = make_continuation_token(session->second.id, relative_path, page.continuation_token);
    return page;
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

PreviousVersionsPage BrowseSessionService::list_previous_versions(
    const std::string& caller_bus_name,
    const std::string& session_id,
    const std::string& profile_id,
    const std::string& source_id,
    const std::string& relative_path,
    const std::string& continuation_token,
    std::size_t requested_entries
) {
    auto session = owned_session(caller_bus_name, session_id);
    if (profile_id != session->second.profile_id.value())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "profile does not match browse session");
    if (source_id.empty())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "source identifier is required");
    if (requested_entries == 0 || requested_entries > maximum_browse_page_entries)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "previous-versions page size must be between 1 and 512");
    const std::size_t offset = previous_versions_offset(
        session->second.id,
        profile_id,
        source_id,
        relative_path,
        continuation_token
    );
    extend(session->second);
    PreviousVersionsPage page = backend_.list_previous_versions(
        session->second.id,
        profile_id,
        source_id,
        relative_path,
        offset,
        requested_entries
    );
    if (page.has_more) {
        if (page.next_offset <= offset)
            throw std::runtime_error("previous-versions backend returned a non-advancing page");
        page.continuation_token = encode_bound_token(
            previous_versions_binding(session->second.id, profile_id, source_id, relative_path),
            std::to_string(page.next_offset)
        );
    } else {
        page.continuation_token.clear();
    }
    return page;
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

btrfsbackup::platform::linux::OwnedFileDescriptor BrowseSessionService::open_entry(
    const std::string& caller_bus_name,
    const std::string& session_id,
    const std::string& relative_path
) {
    auto session = owned_session(caller_bus_name, session_id);
    extend(session->second);
    return backend_.open_entry(session->second.id, relative_path);
}

std::string BrowseSessionService::inspect_repository(
    const std::string& caller_bus_name,
    const std::string& session_id
) {
    auto session = owned_session(caller_bus_name, session_id);
    extend(session->second);
    return backend_.inspect_repository(session->second.id);
}

void BrowseSessionService::close_session(std::map<std::string, Session>::iterator session, BrowseSessionCloseReason reason) {
    session->second.operation_leases.clear();
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
    session->second.operation_leases.clear();
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

void BrowseSessionService::begin_target_eject(const ProfileId& profile_id) {
    if (!ejecting_profiles_.insert(profile_id).second)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "backup target is already being ejected");
    browse_reopen_after_.erase(profile_id);
    const bool active_operation = std::ranges::any_of(sessions_, [&](const auto& item) {
        return item.second.profile_id == profile_id && !item.second.operation_leases.empty();
    });
    if (active_operation) {
        ejecting_profiles_.erase(profile_id);
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Busy,
            "backup browsing operation is active"
        );
    }
    try {
        for (auto session = sessions_.begin(); session != sessions_.end();) {
            if (session->second.profile_id != profile_id) {
                ++session;
                continue;
            }
            auto current = session++;
            close_session(current, BrowseSessionCloseReason::TargetEject);
        }
    } catch (...) {
        ejecting_profiles_.erase(profile_id);
        throw;
    }
}

void BrowseSessionService::end_target_eject(const ProfileId& profile_id) noexcept {
    ejecting_profiles_.erase(profile_id);
    try {
        browse_reopen_after_[profile_id] = steady_clock_() + browse_reopen_delay;
    } catch (...) {
        browse_reopen_after_.erase(profile_id);
    }
}

void BrowseSessionService::expire() noexcept {
    try {
        backend_.cleanup_stale();
    } catch (...) {}
    const auto now = steady_clock_();
    for (auto session = sessions_.begin(); session != sessions_.end();) {
        if (!session->second.operation_leases.empty() || session->second.deadline > now) {
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
