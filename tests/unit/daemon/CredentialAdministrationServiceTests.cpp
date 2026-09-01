// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/CredentialAdministrationService.hpp>

#include <daemon/dbus/ManagerErrors.hpp>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::daemon::control::CredentialAdministrationService;
using btrfsbackup::daemon::control::ICredentialAdministrationBackend;
using btrfsbackup::daemon::control::IManagerAuthorizer;
using btrfsbackup::daemon::control::ManagerAuthorizationAction;
using btrfsbackup::daemon::control::TargetCredential;

class Authorizer final : public IManagerAuthorizer {
  public:
    bool allowed = true;
    std::vector<ManagerAuthorizationAction> actions;

    bool authorize(const std::string&, ManagerAuthorizationAction action) override {
        actions.push_back(action);
        return allowed;
    }
    bool caller_is_active(const std::string&) override {
        return true;
    }
};

class Backend final : public ICredentialAdministrationBackend {
  public:
    std::vector<TargetCredential> credentials{{"slot-0", "Primary password", "passphrase", 0, true, false}};
    std::string operation;
    std::string label;
    int first_fd = -1;
    int second_fd = -1;
    bool automatic = false;

    std::vector<TargetCredential> list_credentials(const btrfsbackup::ProfileId&) const override {
        return credentials;
    }
    void add_passphrase(const btrfsbackup::ProfileId&, int authorization_fd, int new_fd, const std::string& value) override {
        operation = "add-passphrase";
        first_fd = authorization_fd;
        second_fd = new_fd;
        label = value;
    }
    void add_key(const btrfsbackup::ProfileId&, int authorization_fd, int key_fd, const std::string& value, bool use_automatically) override {
        operation = "add-key";
        first_fd = authorization_fd;
        second_fd = key_fd;
        label = value;
        automatic = use_automatically;
    }
    void generate_key(const btrfsbackup::ProfileId&, int authorization_fd, const std::string& value, bool use_automatically) override {
        operation = "generate-key";
        first_fd = authorization_fd;
        label = value;
        automatic = use_automatically;
    }
    void remove_credential(const btrfsbackup::ProfileId&, const std::string& id, int authorization_fd) override {
        operation = "remove:" + id;
        first_fd = authorization_fd;
    }
    void register_initial_passphrase(const btrfsbackup::ProfileId&, int, const std::string&) override {
    }
};

void test_listing_does_not_require_authorization() {
    Authorizer authorizer;
    authorizer.allowed = false;
    Backend backend;
    const auto result = CredentialAdministrationService(authorizer, backend).list_credentials("default");
    test_helpers::expect_true("credential listing", result.size() == 1, "credential was not returned");
    test_helpers::expect_true("listing authorization", authorizer.actions.empty(), "listing requested authorization");
}

void test_mutations_use_dedicated_authorization_and_descriptors() {
    Authorizer authorizer;
    Backend backend;
    CredentialAdministrationService service(authorizer, backend);
    static_cast<void>(service.add_passphrase(":1.2", "default", 11, 12, "  Recovery password  "));
    test_helpers::expect_eq("passphrase operation", backend.operation, "add-passphrase");
    test_helpers::expect_eq("trimmed credential label", backend.label, "Recovery password");
    test_helpers::expect_true("authorization fd", backend.first_fd == 11, "authorization descriptor changed");
    test_helpers::expect_true("new secret fd", backend.second_fd == 12, "new descriptor changed");
    test_helpers::expect_true(
        "credential authorization",
        authorizer.actions == std::vector{ManagerAuthorizationAction::ManageTargetCredentials},
        "wrong authorization action"
    );

    static_cast<void>(service.generate_key(":1.2", "default", 13, "This computer", true));
    test_helpers::expect_eq("generate operation", backend.operation, "generate-key");
    test_helpers::expect_true("automatic key", backend.automatic, "automatic key flag was lost");
}

void test_denied_mutation_never_reaches_backend() {
    Authorizer authorizer;
    authorizer.allowed = false;
    Backend backend;
    try {
        static_cast<void>(CredentialAdministrationService(authorizer, backend).remove_credential(":1.3", "default", "slot-0", 14));
        test_helpers::fail("denied credential mutation", "operation was accepted");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError& error) {
        test_helpers::expect_true(
            "credential denial code",
            error.code() == btrfsbackup::daemon::dbus::ManagerErrorCode::NotAuthorized,
            "wrong denial code"
        );
    }
    test_helpers::expect_true("denied backend", backend.operation.empty(), "backend received denied operation");
}

} // namespace

int main() {
    test_listing_does_not_require_authorization();
    test_mutations_use_dedicated_authorization_and_descriptors();
    test_denied_mutation_never_reaches_backend();
    return test_helpers::finish("credential administration service tests");
}
