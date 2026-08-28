// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <daemon/manager_errors.hpp>
#include <daemon/operational_control_service.hpp>

#include "support/test_helpers.hpp"

namespace {

using btrfsbackup::ProfileId;
using btrfsbackup::RunId;
using btrfsbackup::daemon::IManagerAuthorizer;
using btrfsbackup::daemon::IOperationalControlBackend;
using btrfsbackup::daemon::ManagerAuthorizationAction;
using btrfsbackup::daemon::ManagerCancellationOutcome;
using btrfsbackup::daemon::ManagerErrorCode;
using btrfsbackup::daemon::ManagerOperationError;
using btrfsbackup::daemon::OperationResult;
using btrfsbackup::daemon::OperationalControlService;

class RecordingAuthorizer final : public IManagerAuthorizer {
  public:
    bool allowed = true;
    std::vector<std::string> callers;
    std::vector<ManagerAuthorizationAction> actions;

    bool authorize(const std::string& caller, ManagerAuthorizationAction action) override {
        callers.push_back(caller);
        actions.push_back(action);
        return allowed;
    }
};

class RecordingBackend final : public IOperationalControlBackend {
  public:
    ManagerCancellationOutcome cancellation = ManagerCancellationOutcome::Accepted;
    std::vector<std::string> effects;

    void start_backup(const ProfileId& profile_id) override {
        effects.push_back("start:" + std::string(profile_id.value()));
    }

    ManagerCancellationOutcome cancel_backup(const ProfileId& profile_id, const RunId& run_id) override {
        effects.push_back("cancel:" + std::string(profile_id.value()) + ":" + std::string(run_id.value()));
        return cancellation;
    }

    void validate_target(const ProfileId& profile_id) override {
        effects.push_back("validate:" + std::string(profile_id.value()));
    }

    void eject_target(const ProfileId& profile_id) override {
        effects.push_back("eject:" + std::string(profile_id.value()));
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
    test_helpers::expect_eq("cancel run id", cancelled.run_id, "20260828T120000Z-1-1");
    test_helpers::expect_true("one authorization per request", authorizer.actions.size() == 4, "wrong authorization count");
    test_helpers::expect_true("one effect per request", backend.effects.size() == 4, "wrong effect count");
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

} // namespace

int main() {
    test_authorized_operations();
    test_denied_operation_has_no_effect();
    test_cancellation_outcomes();
    return test_helpers::finish("operational control service tests");
}
