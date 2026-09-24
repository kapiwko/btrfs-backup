// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProfileAdministrationService.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include <config/json/JsonIo.hpp>
#include <core/Errors.hpp>

#include <algorithm>
#include <chrono>
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
using btrfsbackup::daemon::control::ProfileSourceCandidate;
using btrfsbackup::daemon::control::SourceSubvolumeState;
using btrfsbackup::daemon::control::UnsupportedProfileIdentity;
using btrfsbackup::daemon::control::manager_authorization_action_id;
using btrfsbackup::daemon::dbus::ManagerErrorCode;
using btrfsbackup::daemon::dbus::ManagerOperationError;

constexpr auto profile_document = R"({
  "profileId":"default",
  "name":"Default",
  "target":{"btrfsUuid":"target-fs"},
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
    int unsupported_inspections = 0;
    int unsupported_retirements = 0;
    bool hooks_allowed = false;
    SourceSubvolumeState source_state = SourceSubvolumeState::Available;
    std::vector<ProfileSourceCandidate> candidates{
        {"work-candidate", "/srv/work", "work-fs", "/srv/work", "/srv/work/.snapshots/btrfs-backup"},
        {"home-candidate", "/home", "home-fs", "/home", "/home/.snapshots/btrfs-backup"},
        {"target-candidate", "/mnt/backup", "target-fs", "/mnt/backup", "/mnt/backup/.snapshots/btrfs-backup"},
    };
    ProfileSourceCandidate descriptor_candidate{
        {},
        "/srv/projects",
        "projects-fs",
        "/srv",
        "/srv/.snapshots/btrfs-backup",
        "projects-subvolume"
    };

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
    UnsupportedProfileIdentity inspect_unsupported_profile(const ProfileId& id) const override {
        ++const_cast<Backend*>(this)->unsupported_inspections;
        if (id != ProfileId{"legacy"})
            throw ManagerOperationError(ManagerErrorCode::NotFound, "profile does not exist");
        return unsupported;
    }
    void retire_unsupported_profile(const UnsupportedProfileIdentity& expected) override {
        ++unsupported_retirements;
        if (expected != unsupported)
            throw ManagerOperationError(ManagerErrorCode::Conflict, "profile configuration changed");
    }
    void set_profile_enabled(const EditableProfile&, bool enabled) override {
        ++saves;
        Json document = Json::parse(current->document);
        document["enabled"] = enabled;
        current->document = document.dump();
    }
    SourceSubvolumeState inspect_source_subvolume(const std::filesystem::path&) const override {
        return source_state;
    }
    std::vector<ProfileSourceCandidate> source_candidates() const override {
        return candidates;
    }
    ProfileSourceCandidate source_candidate_from_descriptor(int, const btrfsbackup::daemon::control::BrowseAccessIdentity&) const override {
        return descriptor_candidate;
    }
    ProfileSourceCandidate resolve_source_candidate(const std::filesystem::path& path) const override {
        if (path == descriptor_candidate.subvolume)
            return descriptor_candidate;
        return IProfileAdministrationBackend::resolve_source_candidate(path);
    }

    UnsupportedProfileIdentity unsupported{"legacy", 4, "legacy-fingerprint"};
};

void expect_error(const std::string& name, ManagerErrorCode code, const std::function<void()>& operation) {
    try {
        operation();
        test_helpers::fail(name, "operation succeeded");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true(name, error.code() == code, "unexpected manager error");
    }
}

void test_target_filesystem_is_not_a_source_candidate() {
    Authorizer authorizer;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    const auto details = service.get_profile_details("default");
    test_helpers::expect_true(
        "target candidate hidden",
        std::none_of(details.source_candidates.begin(), details.source_candidates.end(), [](const ProfileSourceCandidate& candidate) {
            return candidate.filesystem_uuid == "target-fs";
        }),
        "backup target was offered as a source"
    );
    expect_error("target candidate rejected", ManagerErrorCode::SourceUnavailable, [&] {
        static_cast<void>(service.add_profile_source(
            ":1.12",
            "default",
            "g1",
            "f1",
            R"({"name":"Backup target","candidateId":"target-candidate","localRetention":7,"remoteRetention":14})"
        ));
    });
    test_helpers::expect_true(
        "target candidate not authorized",
        authorizer.actions.empty(),
        "backup target reached authorization"
    );
    test_helpers::expect_true("target candidate not saved", backend.saves == 0, "backup target was saved as a source");
}

void test_details_do_not_request_authorization() {
    Authorizer authorizer;
    authorizer.allowed = false;
    Backend backend;
    const auto details = ProfileAdministrationService(authorizer, backend).get_profile_details("default");
    test_helpers::expect_eq("details profile", details.profile_id, "default");
    test_helpers::expect_true("details authorization", authorizer.actions.empty(), "details requested authorization");
    test_helpers::expect_true("configuration valid", details.configuration_valid, "valid source was rejected");
    test_helpers::expect_true(
        "source candidates",
        details.source_candidates.size() == 1 && details.source_candidates.front().id == "work-candidate",
        "configured sources were not filtered"
    );
}

void test_invalid_existing_and_new_sources_are_reported() {
    Authorizer authorizer;
    Backend backend;
    backend.source_state = SourceSubvolumeState::Missing;
    ProfileAdministrationService service(authorizer, backend);
    const auto details = service.get_profile_details("default");
    test_helpers::expect_true("invalid existing source", !details.configuration_valid, "missing source was accepted");
    test_helpers::expect_eq("missing source code", details.configuration_error_code, "configuration.source_missing");
    try {
        static_cast<void>(service.add_profile_source(
            ":1.12",
            "default",
            "g1",
            "f1",
            R"({"name":"Missing","candidateId":"work-candidate","localRetention":7,"remoteRetention":14})"
        ));
        test_helpers::fail("missing new source", "missing source was saved");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true("missing source code", error.code() == ManagerErrorCode::SourceMissing, "wrong source error");
    }
    test_helpers::expect_true("missing source not authorized", authorizer.actions.empty(), "invalid source requested authorization");
    test_helpers::expect_true("missing source not saved", backend.saves == 0, "invalid source was committed");
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
        R"({"name":"Work files","candidateId":"work-candidate","localRetention":7,"remoteRetention":14})"
    );
    Json document = Json::parse(added.document);
    test_helpers::expect_eq("generated source id", document.at("sources").at(1).at("id").get<std::string>(), "work-files");
    test_helpers::expect_eq(
        "derived snapshot path",
        document.at("sources").at(1).at("localSnapshotDir").get<std::string>(),
        "/srv/work/.snapshots/btrfs-backup/work-files"
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

void test_source_candidate_is_revalidated_after_authorization() {
    Authorizer authorizer;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    authorizer.during = [&](ManagerAuthorizationAction) {
        backend.candidates.front().filesystem_uuid = "replacement-fs";
    };
    expect_error("changed source candidate", ManagerErrorCode::Conflict, [&] {
        static_cast<void>(service.add_profile_source(
            ":1.12",
            "default",
            "g1",
            "f1",
            R"({"name":"Work files","candidateId":"work-candidate","localRetention":7,"remoteRetention":14})"
        ));
    });
    test_helpers::expect_true(
        "changed source candidate not saved",
        backend.saves == 0,
        "changed source candidate reached profile commit"
    );
}

void test_descriptor_candidate_is_caller_bound_and_revalidated() {
    Authorizer authorizer;
    Backend backend;
    auto now = std::chrono::steady_clock::time_point{};
    ProfileAdministrationService service(
        authorizer,
        backend,
        std::chrono::minutes(5),
        [] { return "opaque-custom-candidate"; },
        [&] { return now; }
    );
    const auto candidate = service.register_source_candidate(
        ":1.20",
        "default",
        42,
        {.uid = 1000, .groups = {1000}}
    );
    test_helpers::expect_eq("custom candidate id", candidate.id, "opaque-custom-candidate");
    expect_error("candidate caller binding", ManagerErrorCode::SourceUnavailable, [&] {
        static_cast<void>(service.add_profile_source(
            ":1.21",
            "default",
            "g1",
            "f1",
            R"({"name":"Projects","candidateId":"opaque-custom-candidate","localRetention":7,"remoteRetention":14})"
        ));
    });
    authorizer.during = [&](ManagerAuthorizationAction) {
        backend.descriptor_candidate.subvolume_uuid = "replacement-subvolume";
    };
    expect_error("custom candidate authorization race", ManagerErrorCode::Conflict, [&] {
        static_cast<void>(service.add_profile_source(
            ":1.20",
            "default",
            "g1",
            "f1",
            R"({"name":"Projects","candidateId":"opaque-custom-candidate","localRetention":7,"remoteRetention":14})"
        ));
    });
    test_helpers::expect_true("custom candidate race not saved", backend.saves == 0, "changed custom source was saved");

    authorizer.during = {};
    backend.descriptor_candidate.subvolume_uuid = "projects-subvolume";
    now += std::chrono::minutes(6);
    expect_error("expired custom candidate", ManagerErrorCode::SourceUnavailable, [&] {
        static_cast<void>(service.add_profile_source(
            ":1.20",
            "default",
            "g1",
            "f1",
            R"({"name":"Projects","candidateId":"opaque-custom-candidate","localRetention":7,"remoteRetention":14})"
        ));
    });
}

void test_descriptor_candidate_is_single_use_and_bounded() {
    Authorizer authorizer;
    Backend backend;
    int next_id = 0;
    ProfileAdministrationService service(
        authorizer,
        backend,
        std::chrono::minutes(5),
        [&] { return "custom-" + std::to_string(++next_id); }
    );
    const auto candidate = service.register_source_candidate(
        ":1.22",
        "default",
        42,
        {.uid = 1000, .groups = {1000}}
    );
    static_cast<void>(service.add_profile_source(
        ":1.22",
        "default",
        "g1",
        "f1",
        "{\"name\":\"Projects\",\"candidateId\":\"" + candidate.id +
            "\",\"localRetention\":7,\"remoteRetention\":14}"
    ));
    test_helpers::expect_true("custom candidate saved", backend.saves == 1, "custom source was not saved");
    expect_error("custom candidate consumed", ManagerErrorCode::SourceUnavailable, [&] {
        static_cast<void>(service.add_profile_source(
            ":1.22",
            "default",
            "g2",
            "f2",
            "{\"name\":\"Projects again\",\"candidateId\":\"" + candidate.id +
                "\",\"localRetention\":7,\"remoteRetention\":14}"
        ));
    });

    for (int index = 0; index < 16; ++index) {
        static_cast<void>(service.register_source_candidate(
            ":1.23",
            "default",
            42,
            {.uid = 1000, .groups = {1000}}
        ));
    }
    expect_error("custom candidate limit", ManagerErrorCode::Conflict, [&] {
        static_cast<void>(service.register_source_candidate(
            ":1.23",
            "default",
            42,
            {.uid = 1000, .groups = {1000}}
        ));
    });
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
    test_helpers::expect_eq(
        "delete action id",
        manager_authorization_action_id(ManagerAuthorizationAction::DeleteProfileConfiguration),
        "io.github.btrfsbackup.delete-profile-configuration"
    );
}

void test_unsupported_retirement_revalidates_after_strong_authorization() {
    Authorizer authorizer;
    Backend backend;
    ProfileAdministrationService service(authorizer, backend);
    service.retire_unsupported_profile(":1.14", "legacy");
    test_helpers::expect_true(
        "unsupported inspected twice",
        backend.unsupported_inspections == 2,
        "unsupported profile was not re-inspected after authorization"
    );
    test_helpers::expect_true(
        "unsupported retired",
        backend.unsupported_retirements == 1,
        "unsupported retirement did not reach the backend"
    );
    test_helpers::expect_true(
        "unsupported delete authorization",
        authorizer.actions == std::vector{ManagerAuthorizationAction::DeleteProfileConfiguration},
        "unsupported retirement used the wrong authorization"
    );

    authorizer.during = [&](ManagerAuthorizationAction) {
        backend.unsupported.fingerprint = "changed-fingerprint";
    };
    expect_error("unsupported authorization race", ManagerErrorCode::Conflict, [&] {
        service.retire_unsupported_profile(":1.14", "legacy");
    });
    test_helpers::expect_true(
        "unsupported race no retirement",
        backend.unsupported_retirements == 1,
        "changed unsupported profile was retired"
    );

    backend.unsupported.fingerprint = "legacy-fingerprint";
    authorizer.during = [&](ManagerAuthorizationAction) {
        backend.unsupported.managed_artifact_manifest_fingerprint = "changed-manifest";
    };
    expect_error("unsupported manifest authorization race", ManagerErrorCode::Conflict, [&] {
        service.retire_unsupported_profile(":1.14", "legacy");
    });
    test_helpers::expect_true(
        "unsupported manifest race no retirement",
        backend.unsupported_retirements == 1,
        "changed retirement manifest was accepted after authorization"
    );

    authorizer.during = {};
    authorizer.allowed = false;
    expect_error("unsupported retirement denied", ManagerErrorCode::NotAuthorized, [&] {
        service.retire_unsupported_profile(":1.14", "legacy");
    });
    test_helpers::expect_true(
        "unsupported denial no retirement",
        backend.unsupported_retirements == 1,
        "denied unsupported retirement reached the backend"
    );
}

} // namespace

int main() {
    test_details_do_not_request_authorization();
    test_invalid_existing_and_new_sources_are_reported();
    test_settings_update_is_bounded_and_preserves_private_fields();
    test_denial_and_authorization_race_have_no_effect();
    test_source_operations_use_stable_identity();
    test_source_candidate_is_revalidated_after_authorization();
    test_descriptor_candidate_is_caller_bound_and_revalidated();
    test_descriptor_candidate_is_single_use_and_bounded();
    test_target_filesystem_is_not_a_source_candidate();
    test_delete_and_activation_keep_dedicated_actions();
    test_unsupported_retirement_revalidates_after_strong_authorization();
    return test_helpers::finish("profile administration service tests");
}
