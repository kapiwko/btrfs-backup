// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <daemon/ManagerErrors.hpp>
#include <daemon/OperationalControlService.hpp>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::ProfileId;
using btrfsbackup::OperationId;
using btrfsbackup::RunId;
using btrfsbackup::config::ConfigurationFingerprint;
using btrfsbackup::config::ConfigurationGeneration;
using btrfsbackup::daemon::AuthorizedOperationContext;
using btrfsbackup::daemon::IManagerAuthorizer;
using btrfsbackup::daemon::IOperationalControlBackend;
using btrfsbackup::daemon::ManagerAuthorizationAction;
using btrfsbackup::daemon::ManagerCancellationOutcome;
using btrfsbackup::daemon::ManagerErrorCode;
using btrfsbackup::daemon::ManagerOperationError;
using btrfsbackup::daemon::OperationResult;
using btrfsbackup::daemon::OperationalResourceVersion;
using btrfsbackup::daemon::OperationalControlService;

class RecordingAuthorizer final : public IManagerAuthorizer {
  public:
    bool allowed = true;
    bool active = true;
    std::string allowed_caller;
    std::function<void()> during_authorization;
    std::vector<std::string> callers;
    std::vector<ManagerAuthorizationAction> actions;

    bool authorize(const std::string& caller, ManagerAuthorizationAction action) override {
        callers.push_back(caller);
        actions.push_back(action);
        if (during_authorization)
            during_authorization();
        return allowed && (allowed_caller.empty() || allowed_caller == caller);
    }

    bool caller_is_active(const std::string& caller) override {
        return active && !callers.empty() && callers.back() == caller;
    }
};

class RecordingBackend final : public IOperationalControlBackend {
  public:
    ManagerCancellationOutcome cancellation = ManagerCancellationOutcome::Accepted;
    OperationalResourceVersion version{
        ConfigurationGeneration{"generation-1"},
        ConfigurationFingerprint{"fingerprint-1"},
    };
    std::vector<std::string> effects;
    std::vector<AuthorizedOperationContext> contexts;

    OperationalResourceVersion inspect_profile(const ProfileId&) const override {
        return version;
    }

    void require_version(const AuthorizedOperationContext& context) const {
        if (version != OperationalResourceVersion{context.generation, context.fingerprint})
            throw ManagerOperationError(ManagerErrorCode::Conflict, "profile changed");
    }

    void start_backup(const AuthorizedOperationContext& context) override {
        require_version(context);
        contexts.push_back(context);
        effects.push_back("start:" + std::string(context.profile_id.value()));
    }

    ManagerCancellationOutcome cancel_backup(
        const RunId& run_id,
        const AuthorizedOperationContext& context
    ) override {
        require_version(context);
        contexts.push_back(context);
        effects.push_back("cancel:" + std::string(context.profile_id.value()) + ":" + std::string(run_id.value()));
        return cancellation;
    }

    void validate_target(const AuthorizedOperationContext& context) override {
        require_version(context);
        contexts.push_back(context);
        effects.push_back("validate:" + std::string(context.profile_id.value()));
    }

    void eject_target(const AuthorizedOperationContext& context) override {
        require_version(context);
        contexts.push_back(context);
        effects.push_back("eject:" + std::string(context.profile_id.value()));
    }
};

void expect_manager_error(
    const std::string& name,
    ManagerErrorCode expected,
    const std::function<void()>& operation
) {
    try {
        operation();
        test_helpers::fail(name, "operation succeeded");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true(name, error.code() == expected, "unexpected manager error");
    }
}

void test_authorized_operations() {
    RecordingAuthorizer authorizer;
    RecordingBackend backend;
    OperationalControlService service(authorizer, backend);

    const OperationResult started = service.start_backup(":1.10", "default");
    const OperationResult cancelled = service.cancel_backup(":1.10", "default", "20260828T120000Z-1-1");
    (void)service.validate_target(":1.10", "default");
    (void)service.eject_target(":1.10", "default");

    test_helpers::expect_eq("start operation", started.operation, "start-backup");
    test_helpers::expect_true("start operation id", !started.operation_id.empty(), "operation id was not returned");
    test_helpers::expect_eq("cancel run id", cancelled.run_id, "20260828T120000Z-1-1");
    test_helpers::expect_true("one authorization per request", authorizer.actions.size() == 4, "wrong authorization count");
    test_helpers::expect_true("one effect per request", backend.effects.size() == 4, "wrong effect count");
    test_helpers::expect_true("one context per effect", backend.contexts.size() == 4, "authorized context was not passed");
    test_helpers::expect_true(
        "unique operation contexts",
        backend.contexts.at(0).operation_id != backend.contexts.at(1).operation_id,
        "separate authorized operations reused an operation id"
    );
    test_helpers::expect_true(
        "authorized context identity",
        backend.contexts.at(0).profile_id == ProfileId{"default"} &&
            backend.contexts.at(0).generation == ConfigurationGeneration{"generation-1"} &&
            backend.contexts.at(0).fingerprint == ConfigurationFingerprint{"fingerprint-1"} &&
            !backend.contexts.at(0).operation_id.value().empty(),
        "authorized context did not preserve the inspected profile identity"
    );
    test_helpers::expect_true(
        "separate authorization actions",
        authorizer.actions.at(0) == ManagerAuthorizationAction::StartBackup &&
            authorizer.actions.at(1) == ManagerAuthorizationAction::CancelBackup &&
            authorizer.actions.at(2) == ManagerAuthorizationAction::ValidateTarget &&
            authorizer.actions.at(3) == ManagerAuthorizationAction::EjectTarget,
        "authorization action was reused"
    );
}

void test_denied_operation_has_no_effect() {
    RecordingAuthorizer authorizer;
    authorizer.allowed = false;
    RecordingBackend backend;
    OperationalControlService service(authorizer, backend);
    expect_manager_error(
        "authorization denial",
        ManagerErrorCode::NotAuthorized,
        [&] { (void)service.start_backup(":1.20", "default"); }
    );
    test_helpers::expect_true("denied effect", backend.effects.empty(), "denied operation reached backend");
}

void test_cancellation_outcomes() {
    RecordingAuthorizer authorizer;
    RecordingBackend backend;
    OperationalControlService service(authorizer, backend);
    backend.cancellation = ManagerCancellationOutcome::StaleRun;
    expect_manager_error(
        "stale cancellation",
        ManagerErrorCode::NotFound,
        [&] { (void)service.cancel_backup(":1.30", "default", "20260828T120000Z-1-1"); }
    );
    backend.cancellation = ManagerCancellationOutcome::RunMismatch;
    expect_manager_error(
        "mismatched cancellation",
        ManagerErrorCode::RunMismatch,
        [&] { (void)service.cancel_backup(":1.30", "default", "20260828T120000Z-1-1"); }
    );
}

void test_authorization_is_caller_bound_and_not_cached() {
    RecordingAuthorizer authorizer;
    authorizer.allowed_caller = ":1.40";
    RecordingBackend backend;
    OperationalControlService service(authorizer, backend);

    (void)service.start_backup(":1.40", "default");
    expect_manager_error(
        "different caller",
        ManagerErrorCode::NotAuthorized,
        [&] { (void)service.start_backup(":1.41", "default"); }
    );
    authorizer.allowed_caller.clear();
    authorizer.allowed = false;
    expect_manager_error(
        "authorization is repeated",
        ManagerErrorCode::NotAuthorized,
        [&] { (void)service.start_backup(":1.40", "default"); }
    );
    test_helpers::expect_true("caller-bound effects", backend.effects.size() == 1, "authorization leaked to a request");
    test_helpers::expect_true("caller-bound checks", authorizer.callers.size() == 3, "request skipped authorization");
}

void test_caller_disconnect_during_authorization() {
    RecordingAuthorizer authorizer;
    RecordingBackend backend;
    authorizer.during_authorization = [&] { authorizer.active = false; };
    OperationalControlService service(authorizer, backend);
    expect_manager_error(
        "caller disconnected",
        ManagerErrorCode::NotAuthorized,
        [&] { (void)service.eject_target(":1.50", "default"); }
    );
    test_helpers::expect_true("disconnected caller effect", backend.effects.empty(), "disconnected caller reached backend");
}

void test_profile_change_during_authorization() {
    RecordingAuthorizer authorizer;
    RecordingBackend backend;
    authorizer.during_authorization = [&] {
        backend.version = {
            ConfigurationGeneration{"generation-2"},
            ConfigurationFingerprint{"fingerprint-2"},
        };
    };
    OperationalControlService service(authorizer, backend);
    expect_manager_error(
        "profile authorization race",
        ManagerErrorCode::Conflict,
        [&] { (void)service.validate_target(":1.60", "default"); }
    );
    test_helpers::expect_true("changed profile effect", backend.effects.empty(), "changed profile reached effect");
}

} // namespace

int main() {
    test_authorized_operations();
    test_denied_operation_has_no_effect();
    test_cancellation_outcomes();
    test_authorization_is_caller_bound_and_not_cached();
    test_caller_disconnect_during_authorization();
    test_profile_change_during_authorization();
    return test_helpers::finish("operational control service tests");
}
