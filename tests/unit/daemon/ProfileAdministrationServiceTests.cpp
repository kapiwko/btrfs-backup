// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProfileAdministrationService.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include <functional>
#include <optional>
#include <vector>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::ProfileId;
using btrfsbackup::daemon::control::EditableProfile;
using btrfsbackup::daemon::control::IManagerAuthorizer;
using btrfsbackup::daemon::control::IProfileAdministrationBackend;
using btrfsbackup::daemon::control::ManagerAuthorizationAction;
using btrfsbackup::daemon::control::ProfileAdministrationService;
using btrfsbackup::daemon::control::ProfileDraftResult;
using btrfsbackup::daemon::control::manager_authorization_action_id;
using btrfsbackup::daemon::dbus::ManagerErrorCode;
using btrfsbackup::daemon::dbus::ManagerOperationError;

class Authorizer final : public IManagerAuthorizer {
  public:
    bool allowed = true;
    bool active = true;
    std::function<void(ManagerAuthorizationAction)> during;
    std::vector<ManagerAuthorizationAction> actions;

    bool authorize(const std::string&, ManagerAuthorizationAction action) override {
        actions.push_back(action);
        if (during)
            during(action);
        return allowed;
    }
    bool caller_is_active(const std::string&) override { return active; }
};

class Backend final : public IProfileAdministrationBackend {
  public:
    std::optional<EditableProfile> current = EditableProfile{"default", "g1", "f1", R"({"profileId":"default"})"};
    int validations = 0;
    int saves = 0;
    int deletes = 0;
    bool hooks_allowed = false;

    std::optional<EditableProfile> find_profile(const ProfileId&) const override { return current; }
    ProfileDraftResult validate_draft(const ProfileId& id, const std::string& document) const override {
        ++const_cast<Backend*>(this)->validations;
        return {std::string(id.value()), {}, {}, document, true};
    }
    ProfileDraftResult save_profile(const EditableProfile&, const ProfileDraftResult& draft, bool allow_hooks) override {
        ++saves;
        hooks_allowed = allow_hooks;
        current = EditableProfile{draft.profile_id, "g2", "f2", draft.document};
        return {draft.profile_id, "g2", "f2", draft.document, true};
    }
    void delete_profile(const EditableProfile&) override {
        ++deletes;
        current.reset();
    }
};

void expect_error(const std::string& name, ManagerErrorCode code, const std::function<void()>& operation) {
    try {
        operation();
        test_helpers::fail(name, "operation succeeded");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true(name, error.code() == code, "unexpected manager error");
    }
}

void test_validation_precedes_authorization_and_denial_has_no_effect() {
    Authorizer authorizer;
    authorizer.allowed = false;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    expect_error("save denied", ManagerErrorCode::NotAuthorized, [&] {
        (void)service.save_profile(":1.10", "default", "g1", "f1", R"({"profileId":"default"})");
    });
    test_helpers::expect_true("draft validated", backend.validations == 1, "draft was not validated before authorization");
    test_helpers::expect_true("denied save", backend.saves == 0, "denied request reached commit");
}

void test_conflicts_before_and_during_authorization() {
    Authorizer authorizer;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    expect_error("stale expected identity", ManagerErrorCode::Conflict, [&] {
        (void)service.save_profile(":1.11", "default", "old", "f1", "{}");
    });
    test_helpers::expect_true("stale request no prompt", authorizer.actions.empty(), "stale request reached authorization");

    authorizer.during = [&](ManagerAuthorizationAction) {
        backend.current->generation = "g2";
        backend.current->fingerprint = "f2";
    };
    expect_error("authorization race", ManagerErrorCode::Conflict, [&] {
        (void)service.save_profile(":1.11", "default", "g1", "f1", "{}");
    });
    test_helpers::expect_true("race no commit", backend.saves == 0, "changed profile was committed");
}

void test_hook_save_requires_both_authorizations() {
    Authorizer authorizer;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    const auto result = service.save_profile_hooks(":1.12", "default", "g1", "f1", "{}");
    test_helpers::expect_eq("hook result generation", result.generation, "g2");
    test_helpers::expect_true("hook backend flag", backend.hooks_allowed, "hook authorization was not propagated");
    test_helpers::expect_true(
        "separate hook actions",
        authorizer.actions == std::vector{
            ManagerAuthorizationAction::SaveProfileHooks,
            ManagerAuthorizationAction::SaveProfileConfiguration,
        },
        "hook save did not require both policies"
    );
    test_helpers::expect_eq(
        "profile save action id",
        manager_authorization_action_id(ManagerAuthorizationAction::SaveProfileConfiguration),
        "io.github.btrfsbackup.save-profile-configuration"
    );
    test_helpers::expect_eq(
        "hook action id",
        manager_authorization_action_id(ManagerAuthorizationAction::SaveProfileHooks),
        "io.github.btrfsbackup.save-profile-hooks"
    );
}

void test_create_and_delete_use_expected_identity() {
    Authorizer authorizer;
    Backend backend;
    backend.current.reset();
    ProfileAdministrationService service(authorizer, backend);
    const auto created = service.save_profile(":1.13", "copy", "", "", R"({"profileId":"copy"})");
    test_helpers::expect_eq("created profile", created.profile_id, "copy");
    service.delete_profile(":1.13", "copy", "g2", "f2");
    test_helpers::expect_true("profile deleted", backend.deletes == 1 && !backend.current.has_value(), "delete was not committed");
}

} // namespace

int main() {
    test_validation_precedes_authorization_and_denial_has_no_effect();
    test_conflicts_before_and_during_authorization();
    test_hook_save_requires_both_authorizations();
    test_create_and_delete_use_expected_identity();
    return test_helpers::finish("profile administration service tests");
}
