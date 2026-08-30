// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <daemon/control/BrowseSessionService.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include "support/TestHelpers.hpp"

namespace {
using namespace std::chrono_literals;
using btrfsbackup::BrowseSessionId;
using btrfsbackup::ProfileId;
using btrfsbackup::daemon::control::BrowseSessionCloseReason;
using btrfsbackup::daemon::control::BrowseSessionEvent;
using btrfsbackup::daemon::control::BrowseSessionService;
using btrfsbackup::daemon::control::IBrowseSessionBackend;
using btrfsbackup::daemon::control::IManagerAuthorizer;
using btrfsbackup::daemon::control::ManagerAuthorizationAction;
using btrfsbackup::daemon::control::OpenedBrowseRoot;
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
    bool caller_is_active(const std::string&) override { return active; }
};

class Backend final : public IBrowseSessionBackend {
  public:
    int stale_cleanups = 0;
    bool fail_close = false;
    std::vector<std::string> opened;
    std::vector<std::string> closed;
    OpenedBrowseRoot open(const ProfileId& profile, const BrowseSessionId& id, std::uint32_t uid) override {
        opened.push_back(std::string(profile.value()) + ":" + std::string(id.value()) + ":" + std::to_string(uid));
        return {"/run/btrfs-backup-browse/" + std::string(id.value()) + "/repository"};
    }
    void close(const BrowseSessionId& id) override {
        closed.push_back(std::string(id.value()));
        if (fail_close)
            throw std::runtime_error("cleanup failed");
    }
    void cleanup_stale() override { ++stale_cleanups; }
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
    BrowseSessionService service(authorizer, backend, 15min, [] { return BrowseSessionId{"browse-one"}; }, {},
        [&](const BrowseSessionEvent& event) { events.push_back(event); });

    const auto session = service.open(":1.10", 1000, "default");
    test_helpers::expect_eq("session id", session.session_id, "browse-one");
    test_helpers::expect_true("read only", session.read_only, "session was not declared read-only");
    test_helpers::expect_true("authorization action", authorizer.actions == std::vector{ManagerAuthorizationAction::OpenBrowseSession}, "wrong authorization action");
    service.close(":1.10", session.session_id);
    test_helpers::expect_true("owned close", backend.closed == std::vector<std::string>{"browse-one"}, "backend was not closed");
    test_helpers::expect_true("close event", events.size() == 1 && events.front().reason == BrowseSessionCloseReason::Requested, "close was not audited");
    test_helpers::expect_true("startup cleanup", backend.stale_cleanups == 1, "stale sessions were not cleaned at startup");
}

void test_denied_and_disconnected_callers_do_not_open() {
    Authorizer authorizer;
    Backend backend;
    BrowseSessionService service(authorizer, backend, 15min, [] { return BrowseSessionId{"browse-denied"}; });
    authorizer.allowed = false;
    expect_error("denied open", ManagerErrorCode::NotAuthorized, [&] { (void)service.open(":1.20", 1000, "default"); });
    authorizer.allowed = true;
    authorizer.active = false;
    expect_error("inactive open", ManagerErrorCode::NotAuthorized, [&] { (void)service.open(":1.20", 1000, "default"); });
    test_helpers::expect_true("denied backend", backend.opened.empty(), "unauthorized caller reached backend");
}

void test_foreign_caller_cannot_close_session() {
    Authorizer authorizer;
    Backend backend;
    BrowseSessionService service(authorizer, backend, 15min, [] { return BrowseSessionId{"browse-owned"}; });
    (void)service.open(":1.30", 1000, "default");
    expect_error("foreign close", ManagerErrorCode::NotAuthorized, [&] { service.close(":1.31", "browse-owned"); });
    test_helpers::expect_true("foreign resource preserved", backend.closed.empty(), "foreign caller closed the session");
    service.close(":1.30", "browse-owned");
}

void test_disconnect_only_closes_callers_sessions() {
    Authorizer authorizer;
    Backend backend;
    int next = 0;
    BrowseSessionService service(authorizer, backend, 15min, [&] {
        return BrowseSessionId{next++ == 0 ? "browse-a" : "browse-b"};
    });
    (void)service.open(":1.40", 1000, "default");
    (void)service.open(":1.41", 1001, "archive");
    service.close_for_caller(":1.40");
    test_helpers::expect_true("disconnect scope", backend.closed == std::vector<std::string>{"browse-a"}, "disconnect closed another caller's session");
    service.close(":1.41", "browse-b");
}

void test_expiration_and_cleanup_failure_are_contained() {
    Authorizer authorizer;
    Backend backend;
    std::vector<BrowseSessionEvent> events;
    auto now = std::chrono::system_clock::time_point{100s};
    BrowseSessionService service(authorizer, backend, 10s, [] { return BrowseSessionId{"browse-expiring"}; },
        [&] { return now; }, [&](const BrowseSessionEvent& event) { events.push_back(event); });
    (void)service.open(":1.50", 1000, "default");
    now += 11s;
    backend.fail_close = true;
    service.expire();
    test_helpers::expect_true("expiration attempted", backend.closed == std::vector<std::string>{"browse-expiring"}, "expired session was not closed");
    test_helpers::expect_true("cleanup failure event", events.size() == 1 && !events.front().succeeded && events.front().reason == BrowseSessionCloseReason::Expired, "cleanup failure was not reported");
    expect_error("expired absent", ManagerErrorCode::NotFound, [&] { service.close(":1.50", "browse-expiring"); });
}

} // namespace

int main() {
    test_authorized_open_and_owned_close();
    test_denied_and_disconnected_callers_do_not_open();
    test_foreign_caller_cannot_close_session();
    test_disconnect_only_closes_callers_sessions();
    test_expiration_and_cleanup_failure_are_contained();
    return test_helpers::finish("browse session service tests");
}
