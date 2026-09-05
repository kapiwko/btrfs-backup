// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <fcntl.h>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <daemon/control/BrowseSessionService.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include "support/TestHelpers.hpp"

namespace {
using btrfsbackup::BrowseSessionId;
using btrfsbackup::ProfileId;
using btrfsbackup::daemon::control::BrowseSessionCloseReason;
using btrfsbackup::daemon::control::BrowseSessionEvent;
using btrfsbackup::daemon::control::BrowseSessionService;
using btrfsbackup::daemon::control::IBrowseSessionBackend;
using btrfsbackup::daemon::control::IManagerAuthorizer;
using btrfsbackup::daemon::control::ManagerAuthorizationAction;
using btrfsbackup::daemon::dbus::ManagerErrorCode;
using btrfsbackup::daemon::dbus::ManagerOperationError;

class Authorizer final : public IManagerAuthorizer {
  public:
    bool allowed = true;
    bool active = true;
    std::vector<ManagerAuthorizationAction> actions;
    bool authorize(const std::string&, ManagerAuthorizationAction action) override {
        actions.push_back(action);
        return allowed;
    }
    bool caller_is_active(const std::string&) override {
        return active;
    }
};

class Backend final : public IBrowseSessionBackend {
  public:
    int stale_cleanups = 0;
    bool fail_close = false;
    std::vector<std::string> opened;
    std::vector<std::string> closed;
    std::string page_after_name;
    std::size_t versions_offset = 0;
    std::string versions_profile;
    std::string versions_source;
    std::filesystem::path versions_path;
    std::vector<std::filesystem::path> coverage_paths;
    void open(const ProfileId& profile, const BrowseSessionId& id, std::uint32_t uid) override {
        opened.push_back(std::string(profile.value()) + ":" + std::string(id.value()) + ":" + std::to_string(uid));
    }
    void close(const BrowseSessionId& id) override {
        closed.push_back(std::string(id.value()));
        if (fail_close)
            throw std::runtime_error("cleanup failed");
    }
    void cleanup_stale() override {
        ++stale_cleanups;
    }
    std::vector<btrfsbackup::daemon::control::BrowseEntryInfo> list_directory(
        const BrowseSessionId&,
        const std::filesystem::path&,
        std::size_t
    ) override {
        return {{"snapshot", true, 0, 0500, 123}};
    }
    btrfsbackup::daemon::control::BrowseDirectoryPage list_directory_page(
        const BrowseSessionId&,
        const std::filesystem::path&,
        const std::string& after_name,
        std::size_t maximum_entries
    ) override {
        page_after_name = after_name;
        if (maximum_entries == 1 && after_name.empty())
            return {{{"alpha", false, 4, 0400, 123}}, "alpha"};
        return {{{"omega", false, 4, 0400, 123}}, {}};
    }
    btrfsbackup::daemon::control::PreviousVersionsPage list_previous_versions(
        const BrowseSessionId&,
        const std::string& profile_id,
        const std::string& source_id,
        const std::filesystem::path& relative_path,
        std::size_t offset,
        std::size_t
    ) override {
        versions_offset = offset;
        versions_profile = profile_id;
        versions_source = source_id;
        versions_path = relative_path;
        return offset == 0
            ? btrfsbackup::daemon::control::PreviousVersionsPage{
                  {{{"new", "2026-09-05T12:00:00Z", {"file.txt", false, 4, 0400, 123}}}},
                  1,
                  true,
                  {},
              }
            : btrfsbackup::daemon::control::PreviousVersionsPage{
                  {{{"old", "2026-09-04T12:00:00Z", {"file.txt", false, 4, 0400, 123}}}},
                  2,
                  false,
                  {},
              };
    }
    btrfsbackup::daemon::control::BrowseEntryInfo inspect_entry(
        const BrowseSessionId&,
        const std::filesystem::path& path
    ) override {
        return {path.filename().string(), false, 4, 0400, 123};
    }
    btrfsbackup::platform::linux::OwnedFileDescriptor open_file(
        const BrowseSessionId&,
        const std::filesystem::path&
    ) override {
        return btrfsbackup::platform::linux::OwnedFileDescriptor(::open("/dev/null", O_RDONLY | O_CLOEXEC));
    }
    btrfsbackup::platform::linux::OwnedFileDescriptor open_entry(
        const BrowseSessionId&,
        const std::filesystem::path&
    ) override {
        return btrfsbackup::platform::linux::OwnedFileDescriptor(::open("/dev/null", O_RDONLY | O_CLOEXEC));
    }
    btrfsbackup::platform::linux::OwnedFileDescriptor open_root(const BrowseSessionId&) override {
        return btrfsbackup::platform::linux::OwnedFileDescriptor(::open("/", O_PATH | O_DIRECTORY | O_CLOEXEC));
    }
    std::string inspect_repository(const BrowseSessionId&) override {
        return R"({"schemaVersion":1,"repositoryId":"test"})";
    }
    std::vector<btrfsbackup::daemon::BackupCoverage> resolve_coverage(
        const std::filesystem::path& path,
        const std::vector<ProfileId>&
    ) override {
        coverage_paths.push_back(path);
        return {};
    }
};

void expect_error(const char* name, ManagerErrorCode code, const std::function<void()>& operation) {
    try {
        operation();
        test_helpers::fail(name, "operation succeeded");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true(name, error.code() == code, "unexpected manager error");
    }
}

void test_authorized_open_and_owned_close() {
    Authorizer authorizer;
    Backend backend;
    std::vector<BrowseSessionEvent> events;
    BrowseSessionService service(authorizer, backend, std::chrono::minutes{15}, [] { return BrowseSessionId{"browse-one"}; }, {}, {}, [&](const BrowseSessionEvent& event) { events.push_back(event); });

    const auto session = service.open(":1.10", 1000, "default");
    test_helpers::expect_eq("session id", session.session_id, "browse-one");
    test_helpers::expect_true("read only", session.read_only, "session was not declared read-only");
    test_helpers::expect_true("authorization action", authorizer.actions == std::vector{ManagerAuthorizationAction::OpenBrowseSession}, "wrong authorization action");
    test_helpers::expect_eq(
        "repository inspection",
        service.inspect_repository(":1.10", session.session_id),
        R"({"schemaVersion":1,"repositoryId":"test"})"
    );
    service.close(":1.10", session.session_id);
    test_helpers::expect_true("owned close", backend.closed == std::vector<std::string>{"browse-one"}, "backend was not closed");
    test_helpers::expect_true("close event", events.size() == 1 && events.front().reason == BrowseSessionCloseReason::Requested, "close was not audited");
    test_helpers::expect_true("startup cleanup", backend.stale_cleanups == 1, "stale sessions were not cleaned at startup");
}

void test_denied_and_disconnected_callers_do_not_open() {
    Authorizer authorizer;
    Backend backend;
    BrowseSessionService service(authorizer, backend, std::chrono::minutes{15}, [] { return BrowseSessionId{"browse-denied"}; });
    authorizer.allowed = false;
    expect_error("denied open", ManagerErrorCode::NotAuthorized, [&] { (void)service.open(":1.20", 1000, "default"); });
    authorizer.allowed = true;
    authorizer.active = false;
    expect_error("inactive open", ManagerErrorCode::NotAuthorized, [&] { (void)service.open(":1.20", 1000, "default"); });
    test_helpers::expect_true("denied backend", backend.opened.empty(), "unauthorized caller reached backend");
}

void test_coverage_requires_browse_authorization() {
    Authorizer authorizer;
    Backend backend;
    BrowseSessionService service(authorizer, backend);

    authorizer.allowed = false;
    expect_error("denied coverage", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.resolve_coverage(":1.21", "/home/other-user/private", {});
    });
    authorizer.allowed = true;
    authorizer.active = false;
    expect_error("inactive coverage", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.resolve_coverage(":1.21", "/root", {});
    });
    authorizer.active = true;
    expect_error("relative coverage", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.resolve_coverage(":1.21", "home/other-user", {});
    });
    test_helpers::expect_true(
        "unauthorized coverage backend",
        backend.coverage_paths.empty(),
        "unauthorized coverage query reached the privileged backend"
    );

    (void)service.resolve_coverage(":1.21", "/home/../root", {});
    test_helpers::expect_true(
        "coverage authorization action",
        authorizer.actions == std::vector{
                                  ManagerAuthorizationAction::OpenBrowseSession,
                                  ManagerAuthorizationAction::OpenBrowseSession,
                                  ManagerAuthorizationAction::OpenBrowseSession,
                              },
        "coverage query used the wrong authorization action"
    );
    test_helpers::expect_true(
        "normalized authorized coverage",
        backend.coverage_paths == std::vector<std::filesystem::path>{"/root"},
        "authorized coverage query was not normalized"
    );
}

void test_foreign_caller_cannot_close_session() {
    Authorizer authorizer;
    Backend backend;
    BrowseSessionService service(authorizer, backend, std::chrono::minutes{15}, [] { return BrowseSessionId{"browse-owned"}; });
    (void)service.open(":1.30", 1000, "default");
    expect_error("foreign renew", ManagerErrorCode::NotAuthorized, [&] { (void)service.renew(":1.31", "browse-owned"); });
    expect_error("foreign lease", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.begin_operation(":1.31", "browse-owned");
    });
    expect_error("foreign close", ManagerErrorCode::NotAuthorized, [&] { service.close(":1.31", "browse-owned"); });
    expect_error("foreign list", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.list_directory(":1.31", "browse-owned", ".");
    });
    expect_error("foreign inspect", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.inspect_entry(":1.31", "browse-owned", "snapshot/file");
    });
    expect_error("foreign open file", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.open_file(":1.31", "browse-owned", "snapshot/file");
    });
    expect_error("foreign open entry", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.open_entry(":1.31", "browse-owned", "snapshot/file");
    });
    expect_error("foreign open root", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.open_root(":1.31", "browse-owned");
    });
    expect_error("foreign repository inspection", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.inspect_repository(":1.31", "browse-owned");
    });
    test_helpers::expect_true("foreign resource preserved", backend.closed.empty(), "foreign caller closed the session");
    service.close(":1.30", "browse-owned");
}

void test_disconnect_only_closes_callers_sessions() {
    Authorizer authorizer;
    Backend backend;
    int next = 0;
    BrowseSessionService service(authorizer, backend, std::chrono::minutes{15}, [&] {
        return BrowseSessionId{next++ == 0 ? "browse-a" : "browse-b"};
    });
    (void)service.open(":1.40", 1000, "default");
    (void)service.open(":1.41", 1001, "archive");
    service.close_for_caller(":1.40");
    test_helpers::expect_true("disconnect scope", backend.closed == std::vector<std::string>{"browse-a"}, "disconnect closed another caller's session");
    service.close(":1.41", "browse-b");
}

void test_target_eject_closes_idle_profile_sessions() {
    Authorizer authorizer;
    Backend backend;
    std::vector<BrowseSessionEvent> events;
    auto now = std::chrono::steady_clock::time_point{std::chrono::seconds{100}};
    int next = 0;
    BrowseSessionService service(
        authorizer,
        backend,
        std::chrono::minutes{15},
        [&] { return BrowseSessionId{"browse-eject-" + std::to_string(++next)}; },
        [&] { return now; },
        {},
        [&](const BrowseSessionEvent& event) { events.push_back(event); }
    );
    (void)service.open(":1.42", 1000, "default");
    (void)service.open(":1.43", 1001, "archive");
    (void)service.open(":1.44", 1002, "default");

    service.begin_target_eject(ProfileId{"default"});

    expect_error("browse reopen during eject", ManagerErrorCode::Busy, [&] {
        (void)service.open(":1.42", 1000, "default");
    });
    (void)service.open(":1.43", 1001, "archive");
    service.end_target_eject(ProfileId{"default"});
    expect_error("stale browse reopen after eject", ManagerErrorCode::Busy, [&] {
        (void)service.open(":1.42", 1000, "default");
    });
    now += std::chrono::seconds{3};
    (void)service.open(":1.42", 1000, "default");

    test_helpers::expect_true(
        "eject profile scope",
        backend.closed == std::vector<std::string>{"browse-eject-1", "browse-eject-3"},
        "target eject left a profile session open or closed another profile"
    );
    test_helpers::expect_true(
        "eject close events",
        events.size() == 2 && events.front().reason == BrowseSessionCloseReason::TargetEject &&
            events.back().reason == BrowseSessionCloseReason::TargetEject,
        "target eject session cleanup was not audited"
    );
    service.close(":1.43", "browse-eject-2");
}

void test_target_eject_preserves_active_browse_operation() {
    Authorizer authorizer;
    Backend backend;
    BrowseSessionService service(
        authorizer,
        backend,
        std::chrono::minutes{15},
        [] { return BrowseSessionId{"browse-eject-active"}; }
    );
    const auto opened = service.open(":1.45", 1000, "default");
    const std::string lease = service.begin_operation(":1.45", opened.session_id);

    expect_error("active browse blocks eject", ManagerErrorCode::Busy, [&] {
        service.begin_target_eject(ProfileId{"default"});
    });
    test_helpers::expect_true("active browse preserved", backend.closed.empty(), "active browse was interrupted");
    service.end_operation(":1.45", opened.session_id, lease);
    service.begin_target_eject(ProfileId{"default"});
    service.end_target_eject(ProfileId{"default"});
}

void test_expiration_and_cleanup_failure_are_contained() {
    Authorizer authorizer;
    Backend backend;
    std::vector<BrowseSessionEvent> events;
    auto now = std::chrono::steady_clock::time_point{std::chrono::seconds{100}};
    BrowseSessionService service(authorizer, backend, std::chrono::seconds{10}, [] { return BrowseSessionId{"browse-expiring"}; }, [&] { return now; }, {}, [&](const BrowseSessionEvent& event) { events.push_back(event); });
    (void)service.open(":1.50", 1000, "default");
    now += std::chrono::seconds{11};
    backend.fail_close = true;
    service.expire();
    test_helpers::expect_true("periodic stale cleanup", backend.stale_cleanups == 2, "stale cleanup was not retried");
    test_helpers::expect_true("expiration attempted", backend.closed == std::vector<std::string>{"browse-expiring"}, "expired session was not closed");
    test_helpers::expect_true("cleanup failure event", events.size() == 1 && !events.front().succeeded && events.front().reason == BrowseSessionCloseReason::Expired, "cleanup failure was not reported");
    backend.fail_close = false;
    service.close(":1.50", "browse-expiring");
    expect_error("closed absent", ManagerErrorCode::NotFound, [&] { service.close(":1.50", "browse-expiring"); });
}

void test_failed_disconnect_clears_leases_for_cleanup_retry() {
    Authorizer authorizer;
    Backend backend;
    BrowseSessionService service(
        authorizer,
        backend,
        std::chrono::minutes{15},
        [] { return BrowseSessionId{"browse-disconnected-active"}; }
    );
    const auto opened = service.open(":1.55", 1000, "default");
    (void)service.begin_operation(":1.55", opened.session_id);
    backend.fail_close = true;
    service.close_for_caller(":1.55");
    backend.fail_close = false;
    service.expire();
    test_helpers::expect_true(
        "disconnected cleanup retried",
        backend.closed == std::vector<std::string>{"browse-disconnected-active", "browse-disconnected-active"},
        "active disconnected session was not retried"
    );
}

void test_renewal_uses_monotonic_deadline_and_operation_lease() {
    Authorizer authorizer;
    Backend backend;
    auto monotonic = std::chrono::steady_clock::time_point{std::chrono::seconds{100}};
    auto wall = std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
    BrowseSessionService service(
        authorizer,
        backend,
        std::chrono::seconds{10},
        [] { return BrowseSessionId{"browse-renew"}; },
        [&] { return monotonic; },
        [&] { return wall; }
    );
    const auto opened = service.open(":1.60", 1000, "default");
    wall -= std::chrono::hours{24};
    monotonic += std::chrono::seconds{9};
    const auto renewed = service.renew(":1.60", opened.session_id);
    test_helpers::expect_true(
        "renewed wall expiry",
        renewed.expires_at != opened.expires_at,
        "renewal did not refresh the display expiry"
    );
    monotonic += std::chrono::seconds{11};
    const std::string lease = service.begin_operation(":1.60", opened.session_id);
    monotonic += std::chrono::hours{1};
    service.expire();
    test_helpers::expect_true("active session pinned", backend.closed.empty(), "active operation expired");
    service.end_operation(":1.60", opened.session_id, lease);
    monotonic += std::chrono::seconds{11};
    service.expire();
    test_helpers::expect_true("inactive session expired", backend.closed.size() == 1, "inactive session remained");
}

void test_operation_leases_are_balanced_bounded_and_owned() {
    Authorizer authorizer;
    Backend backend;
    auto monotonic = std::chrono::steady_clock::time_point{std::chrono::seconds{100}};
    int session_number = 0;
    int lease_number = 0;
    BrowseSessionService service(
        authorizer,
        backend,
        std::chrono::seconds{10},
        [&] { return BrowseSessionId{"browse-lease-" + std::to_string(++session_number)}; },
        [&] { return monotonic; },
        {},
        {},
        4,
        2,
        2,
        [&] { return "lease-" + std::to_string(++lease_number); }
    );
    const auto first = service.open(":1.62", 1000, "default");
    const auto second = service.open(":1.63", 1001, "archive");
    const std::string first_lease = service.begin_operation(":1.62", first.session_id);
    const std::string second_lease = service.begin_operation(":1.62", first.session_id);
    const std::string other_session_lease = service.begin_operation(":1.63", second.session_id);
    expect_error("lease limit", ManagerErrorCode::Busy, [&] {
        (void)service.begin_operation(":1.62", first.session_id);
    });
    expect_error("foreign lease release", ManagerErrorCode::NotAuthorized, [&] {
        service.end_operation(":1.63", first.session_id, first_lease);
    });
    expect_error("wrong session lease release", ManagerErrorCode::InvalidRequest, [&] {
        service.end_operation(":1.63", second.session_id, first_lease);
    });
    monotonic += std::chrono::hours{1};
    service.expire();
    test_helpers::expect_true("leased session remains", backend.closed.empty(), "leased session expired");
    service.end_operation(":1.62", first.session_id, first_lease);
    expect_error("double lease release", ManagerErrorCode::InvalidRequest, [&] {
        service.end_operation(":1.62", first.session_id, first_lease);
    });
    service.end_operation(":1.62", first.session_id, second_lease);
    service.end_operation(":1.63", second.session_id, other_session_lease);
    monotonic += std::chrono::seconds{11};
    service.expire();
    test_helpers::expect_true(
        "released leases expire",
        backend.closed == std::vector<std::string>{first.session_id, second.session_id},
        "released sessions remained active"
    );
}

void test_session_limits_are_enforced_before_backend_open() {
    Authorizer authorizer;
    Backend backend;
    int next = 0;
    BrowseSessionService service(
        authorizer,
        backend,
        std::chrono::minutes{15},
        [&] { return BrowseSessionId{"browse-limit-" + std::to_string(++next)}; },
        {},
        {},
        {},
        2,
        1
    );
    static_cast<void>(service.open(":1.70", 1000, "default"));
    expect_error("per caller limit", ManagerErrorCode::Busy, [&] {
        static_cast<void>(service.open(":1.70", 1000, "archive"));
    });
    static_cast<void>(service.open(":1.71", 1000, "archive"));
    expect_error("global session limit", ManagerErrorCode::Busy, [&] {
        static_cast<void>(service.open(":1.72", 1001, "third"));
    });
    test_helpers::expect_true("limited backend opens", backend.opened.size() == 2, "limit reached backend open");
}

void test_directory_pages_bind_tokens_to_session_and_path() {
    Authorizer authorizer;
    Backend backend;
    int next = 0;
    BrowseSessionService service(authorizer, backend, std::chrono::minutes{15}, [&] {
        return BrowseSessionId{"browse-page-" + std::to_string(++next)};
    });
    const auto first_session = service.open(":1.80", 1000, "default");
    const auto second_session = service.open(":1.80", 1000, "archive");
    const auto first = service.list_directory_page(":1.80", first_session.session_id, "snapshot", "", 1);
    test_helpers::expect_true(
        "first page token",
        first.entries.size() == 1 && first.entries.front().name == "alpha" && !first.continuation_token.empty(),
        "first page did not return a continuation token"
    );
    const auto second = service.list_directory_page(
        ":1.80",
        first_session.session_id,
        "snapshot",
        first.continuation_token,
        1
    );
    test_helpers::expect_true(
        "continued page",
        backend.page_after_name == "alpha" && second.entries.size() == 1 && second.continuation_token.empty(),
        "continuation token did not resume after the last name"
    );
    expect_error("token path binding", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_directory_page(":1.80", first_session.session_id, "other", first.continuation_token, 1);
    });
    expect_error("token session binding", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_directory_page(":1.80", second_session.session_id, "snapshot", first.continuation_token, 1);
    });
    expect_error("malformed token", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_directory_page(":1.80", first_session.session_id, "snapshot", "v1:not-hex", 1);
    });
    expect_error("zero page size", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_directory_page(":1.80", first_session.session_id, "snapshot", "", 0);
    });
    expect_error("oversized page", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_directory_page(":1.80", first_session.session_id, "snapshot", "", 513);
    });
}

void test_previous_versions_pages_bind_tokens_to_session_and_query() {
    Authorizer authorizer;
    Backend backend;
    int next = 0;
    BrowseSessionService service(authorizer, backend, std::chrono::minutes{15}, [&] {
        return BrowseSessionId{"browse-versions-" + std::to_string(++next)};
    });
    const auto first_session = service.open(":1.90", 1000, "default");
    const auto second_session = service.open(":1.90", 1000, "default");
    const auto first = service.list_previous_versions(
        ":1.90",
        first_session.session_id,
        "default",
        "home",
        "documents/file.txt",
        "",
        1
    );
    test_helpers::expect_true(
        "previous versions first page",
        first.entries.size() == 1 && first.entries.front().snapshot_id == "new" && !first.continuation_token.empty() &&
            backend.versions_profile == "default" && backend.versions_source == "home" &&
            backend.versions_path == "documents/file.txt",
        "previous versions query was not forwarded or paged"
    );
    const auto second = service.list_previous_versions(
        ":1.90",
        first_session.session_id,
        "default",
        "home",
        "documents/file.txt",
        first.continuation_token,
        1
    );
    test_helpers::expect_true(
        "previous versions continuation",
        backend.versions_offset == 1 && second.entries.size() == 1 && second.continuation_token.empty(),
        "previous versions continuation did not advance"
    );
    expect_error("previous versions profile binding", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_previous_versions(":1.90", first_session.session_id, "archive", "home", "documents/file.txt", "", 1);
    });
    expect_error("previous versions source binding", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_previous_versions(":1.90", first_session.session_id, "default", "root", "documents/file.txt", first.continuation_token, 1);
    });
    expect_error("previous versions path binding", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_previous_versions(":1.90", first_session.session_id, "default", "home", "other", first.continuation_token, 1);
    });
    expect_error("previous versions session binding", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_previous_versions(":1.90", second_session.session_id, "default", "home", "documents/file.txt", first.continuation_token, 1);
    });
    expect_error("previous versions zero page", ManagerErrorCode::InvalidRequest, [&] {
        (void)service.list_previous_versions(":1.90", first_session.session_id, "default", "home", ".", "", 0);
    });
    expect_error("foreign previous versions", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.list_previous_versions(":1.91", first_session.session_id, "default", "home", ".", "", 1);
    });
}

} // namespace

int main() {
    test_authorized_open_and_owned_close();
    test_denied_and_disconnected_callers_do_not_open();
    test_coverage_requires_browse_authorization();
    test_foreign_caller_cannot_close_session();
    test_disconnect_only_closes_callers_sessions();
    test_target_eject_closes_idle_profile_sessions();
    test_target_eject_preserves_active_browse_operation();
    test_expiration_and_cleanup_failure_are_contained();
    test_failed_disconnect_clears_leases_for_cleanup_retry();
    test_renewal_uses_monotonic_deadline_and_operation_lease();
    test_operation_leases_are_balanced_bounded_and_owned();
    test_session_limits_are_enforced_before_backend_open();
    test_directory_pages_bind_tokens_to_session_and_path();
    test_previous_versions_pages_bind_tokens_to_session_and_query();
    return test_helpers::finish("browse session service tests");
}
