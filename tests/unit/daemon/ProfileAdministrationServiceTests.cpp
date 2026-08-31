// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProfileAdministrationService.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include <config/json/JsonIo.hpp>

#include <functional>
#include <optional>
#include <vector>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::ProfileId;
using btrfsbackup::config::json::Json;
using btrfsbackup::daemon::control::EditableProfile;
using btrfsbackup::daemon::control::IManagerAuthorizer;
using btrfsbackup::daemon::control::IProfileAdministrationBackend;
using btrfsbackup::daemon::control::ManagerAuthorizationAction;
using btrfsbackup::daemon::control::ProfileAdministrationService;
using btrfsbackup::daemon::control::ProfileDraftResult;
using btrfsbackup::daemon::control::manager_authorization_action_id;
using btrfsbackup::daemon::dbus::ManagerErrorCode;
using btrfsbackup::daemon::dbus::ManagerOperationError;

constexpr auto profile_document = R"({
  "profileId":"default",
  "name":"Default",
  "settings":{"dailyLimit":true,"autoEject":true,"incrementalRequired":true},
  "sources":[{
    "id":"home","name":"Home","enabled":true,"subvolume":"/home",
    "localSnapshotDir":"/.snapshots/btrfs-backup/home","remoteSubdir":"home",
    "localRetention":30,"remoteRetention":30
  }],
  "hooks":{"beforeSnapshot":["private-command"]}
})";

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
    bool caller_is_active(const std::string&) override {
        return active;
    }
};

class Backend final : public IProfileAdministrationBackend {
  public:
    std::optional<EditableProfile> current = EditableProfile{"default", "g1", "f1", profile_document};
    int validations = 0;
    int saves = 0;
    int deletes = 0;
    bool hooks_allowed = false;

    std::optional<EditableProfile> find_profile(const ProfileId&) const override {
        return current;
    }
    ProfileDraftResult validate_draft(const ProfileId& id, const std::string& document) const override {
        ++const_cast<Backend*>(this)->validations;
        const Json parsed = Json::parse(document);
        static_cast<void>(parsed);
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
    void set_profile_enabled(const EditableProfile&, bool enabled) override {
        ++saves;
        Json document = Json::parse(current->document);
        document["enabled"] = enabled;
        current->document = document.dump();
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

void test_details_do_not_request_authorization() {
    Authorizer authorizer;
    authorizer.allowed = false;
    Backend backend;
    const auto details = ProfileAdministrationService(authorizer, backend).get_profile_details("default");
    test_helpers::expect_eq("details profile", details.profile_id, "default");
    test_helpers::expect_true("details authorization", authorizer.actions.empty(), "details requested authorization");
}

void test_settings_update_is_bounded_and_preserves_private_fields() {
    Authorizer authorizer;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    const auto result = service.update_profile_settings(
        ":1.10",
        "default",
        "g1",
        "f1",
        R"({"name":"Laptop","dailyLimit":false,"autoEject":false})"
    );
    const Json document = Json::parse(result.document);
    test_helpers::expect_eq("updated name", document.at("name").get<std::string>(), "Laptop");
    test_helpers::expect_true("updated daily limit", !document.at("settings").at("dailyLimit").get<bool>(), "daily limit unchanged");
    test_helpers::expect_true("technical setting preserved", document.at("settings").at("incrementalRequired").get<bool>(), "technical field was lost");
    test_helpers::expect_true("hooks preserved", document.contains("hooks"), "private hooks were lost");
    test_helpers::expect_true("hooks not authorized", !backend.hooks_allowed, "domain update enabled hook changes");
    test_helpers::expect_true(
        "single manage action",
        authorizer.actions == std::vector{ManagerAuthorizationAction::ManageProfileConfiguration},
        "settings update used the wrong policy"
    );
}

void test_denial_and_authorization_race_have_no_effect() {
    Authorizer authorizer;
    authorizer.allowed = false;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    expect_error("save denied", ManagerErrorCode::NotAuthorized, [&] {
        static_cast<void>(service.update_profile_settings(
            ":1.11",
            "default",
            "g1",
            "f1",
            R"({"name":"Denied","dailyLimit":true,"autoEject":true})"
        ));
    });
    test_helpers::expect_true("request validated", backend.validations == 1, "request was not validated before authorization");
    test_helpers::expect_true("denied save", backend.saves == 0, "denied request reached commit");

    authorizer.allowed = true;
    authorizer.during = [&](ManagerAuthorizationAction) {
        backend.current->generation = "g2";
        backend.current->fingerprint = "f2";
    };
    expect_error("authorization race", ManagerErrorCode::Conflict, [&] {
        static_cast<void>(service.update_profile_settings(
            ":1.11",
            "default",
            "g1",
            "f1",
            R"({"name":"Race","dailyLimit":true,"autoEject":true})"
        ));
    });
    test_helpers::expect_true("race no commit", backend.saves == 0, "changed profile was committed");
}

void test_source_operations_use_stable_identity() {
    Authorizer authorizer;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    auto added = service.add_profile_source(
        ":1.12",
        "default",
        "g1",
        "f1",
        R"({"name":"Work files","subvolume":"/srv/work","localRetention":7,"remoteRetention":14})"
    );
    Json document = Json::parse(added.document);
    test_helpers::expect_eq("generated source id", document.at("sources").at(1).at("id").get<std::string>(), "work-files");
    test_helpers::expect_eq(
        "derived snapshot path",
        document.at("sources").at(1).at("localSnapshotDir").get<std::string>(),
        "/srv/.snapshots/btrfs-backup/work-files"
    );

    auto updated = service.update_profile_source(
        ":1.12",
        "default",
        "work-files",
        "g2",
        "f2",
        R"({"name":"Projects","localRetention":10,"remoteRetention":20})"
    );
    document = Json::parse(updated.document);
    test_helpers::expect_eq("source identity preserved", document.at("sources").at(1).at("id").get<std::string>(), "work-files");
    test_helpers::expect_eq("source renamed", document.at("sources").at(1).at("name").get<std::string>(), "Projects");

    const auto removed = service.remove_profile_source(":1.12", "default", "work-files", "g2", "f2");
    document = Json::parse(removed.document);
    test_helpers::expect_true("source removed", document.at("sources").size() == 1, "source remained in profile");
}

void test_delete_and_activation_keep_dedicated_actions() {
    Authorizer authorizer;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    service.set_profile_enabled(":1.13", "default", false);
    service.delete_profile(":1.13", "default", "g1", "f1");
    test_helpers::expect_true("profile deleted", backend.deletes == 1, "delete was not committed");
    test_helpers::expect_eq(
        "manage action id",
        manager_authorization_action_id(ManagerAuthorizationAction::ManageProfileConfiguration),
        "io.github.btrfsbackup.manage-profile-configuration"
    );
}

} // namespace

int main() {
    test_details_do_not_request_authorization();
    test_settings_update_is_bounded_and_preserves_private_fields();
    test_denial_and_authorization_race_have_no_effect();
    test_source_operations_use_stable_identity();
    test_delete_and_activation_keep_dedicated_actions();
    return test_helpers::finish("profile administration service tests");
}
