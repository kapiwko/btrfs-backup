// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemProfileAdministrationBackend.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include <core/Errors.hpp>
#include <config/json/JsonIo.hpp>
#include <config/json/ProfileDocument.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <platform/linux/config/ProfileConfigurationTransaction.hpp>
#include <platform/linux/config/ProfileService.hpp>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::ProfileId;
using btrfsbackup::daemon::control::SystemProfileAdministrationBackend;
using btrfsbackup::daemon::dbus::ManagerOperationError;
namespace json = btrfsbackup::config::json;
namespace linux_config = btrfsbackup::platform::linux::config;

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

class FakeBtrfsOperations final : public btrfsbackup::backup::IBtrfsOperations {
  public:
    bool is_subvolume(const std::filesystem::path&) override {
        return true;
    }
    std::optional<btrfsbackup::backup::SnapshotMetadata> read_snapshot_metadata(const std::filesystem::path&) override {
        return std::nullopt;
    }
    void create_readonly_snapshot(const std::filesystem::path&, const std::filesystem::path&) override {
    }
    void delete_subvolume(const std::filesystem::path&) override {
    }
};

class FailingActivator final : public btrfsbackup::config::IConfigurationActivator {
  public:
    void activate() override {
        if (++calls == 1)
            throw btrfsbackup::ValidationError("injected retirement activation failure");
    }

    int calls = 0;
};

json::Json profile_document() {
    return {
        {"schemaVersion", 1},
        {"profileId", "default"},
        {"name", "Default"},
        {"enabled", true},
        {"target", {
                       {"device", "/dev/null"},
                       {"luksUuid", "11111111-2222-3333-4444-555555555555"},
                       {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
                       {"partitionUuid", ""},
                       {"serial", ""},
                       {"mapperName", "backupdisk"},
                       {"activation", {{"mode", "keyFile"}, {"keyFile", "/etc/btrfs-backup/key.secret"}}},
                   }},
        {"paths", {{"remoteRoot", "/snapshots"}, {"incomingRoot", "/.incoming"}}},
        {"settings", json::Json::object()},
        {"hooks", {{"beforeSnapshot", json::Json::array()}, {"afterSnapshot", json::Json::array()}}},
        {"sources", json::Json::array({{
                        {"id", "home"},
                        {"name", "Home"},
                        {"enabled", true},
                        {"subvolume", "/home"},
                        {"localSnapshotDir", "/.snapshots/home"},
                        {"remoteSubdir", "home"},
                        {"remoteRetention", 2},
                        {"localRetention", 2},
                    }})},
    };
}

void test_backend_preserves_secrets_and_hook_boundary() {
    const auto root = test_helpers::test_root("profile-administration", "backend");
    const linux_config::ProfileInstallationRoots roots{
        root / "etc",
        root / "udev",
        root / "systemd",
        root / "public"
    };
    btrfsbackup::config::NullConfigurationActivator activator;
    FakeBtrfsOperations btrfs;
    const auto initial = json::profile_from_json(profile_document(), root / "mounts");
    linux_config::install_profile(initial, roots, activator);
    test_helpers::write_file(root / "etc" / "key.secret", "TOP-SECRET-KEY-CONTENTS");

    SystemProfileAdministrationBackend backend(
        {
            .etc_root = roots.etc_root,
            .udev_root = roots.udev_root,
            .systemd_root = roots.systemd_root,
            .public_root = roots.public_root,
            .state_root = root / "state",
            .status_root = root / "status",
            .history_root = root / "history",
        },
        root / "mounts",
        "/proc/self/mountinfo",
        btrfs,
        activator
    );
    const auto current = backend.find_profile(ProfileId{"default"});
    test_helpers::expect_true("profile loaded", current.has_value(), "installed profile was not found");
    test_helpers::expect_true(
        "secret contents hidden",
        current->document.find("TOP-SECRET-KEY-CONTENTS") == std::string::npos,
        "key contents escaped through the editing API"
    );
    try {
        static_cast<void>(backend.inspect_unsupported_profile(ProfileId{"default"}));
        test_helpers::fail("supported retirement", "supported profile was accepted for retirement");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true(
            "supported retirement error",
            error.code() == btrfsbackup::daemon::dbus::ManagerErrorCode::InvalidRequest,
            "supported profile returned the wrong retirement error"
        );
    }

    auto changed = json::Json::parse(current->document);
    changed["hooks"]["beforeSnapshot"] = json::Json::array({{
        {"type", "program"},
        {"program", "/etc/btrfs-backup/hooks.d/test"},
        {"arguments", json::Json::array()},
        {"timeoutSeconds", 30},
    }});
    const auto draft = backend.validate_draft(ProfileId{"default"}, changed.dump());
    try {
        (void)backend.save_profile(*current, draft, false);
        test_helpers::fail("ordinary hook save", "hook change was accepted");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true(
            "ordinary hook policy",
            error.code() == btrfsbackup::daemon::dbus::ManagerErrorCode::NotAuthorized,
            "hook change returned the wrong policy error"
        );
    }
    const auto saved = backend.save_profile(*current, draft, true);
    test_helpers::expect_true("new generation", saved.generation != current->generation, "save reused configuration generation");
    test_helpers::expect_true("new fingerprint", saved.fingerprint != current->fingerprint, "save reused fingerprint");

    try {
        backend.delete_profile(*current);
        test_helpers::fail("stale delete", "stale identity was accepted");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true(
            "stale delete conflict",
            error.code() == btrfsbackup::daemon::dbus::ManagerErrorCode::Conflict,
            "stale identity returned the wrong error"
        );
    }
    backend.set_profile_enabled({saved.profile_id, saved.generation, saved.fingerprint, saved.document}, false);
    const auto disabled = backend.find_profile(ProfileId{"default"});
    test_helpers::expect_true("disabled profile loaded", disabled.has_value(), "disabled profile disappeared");
    test_helpers::expect_true(
        "profile disabled",
        !json::Json::parse(disabled->document).at("enabled").get<bool>(),
        "enabled flag was not updated"
    );
    test_helpers::expect_true(
        "public activation disabled",
        !json::load_json_file(root / "public" / "default.json").at("enabled").get<bool>(),
        "public activation state remained enabled"
    );
    backend.delete_profile(*disabled);
    test_helpers::expect_true("private profile removed", !std::filesystem::exists(root / "etc" / "profiles" / "default" / "profile.json"), "private profile remains");
    test_helpers::expect_true("public profile removed", !std::filesystem::exists(root / "public" / "default.json"), "public marker remains");
}

void test_unsupported_profile_retirement_is_bounded_and_fingerprint_pinned() {
    const auto root = test_helpers::test_root("profile-administration", "unsupported-retirement");
    const linux_config::ProfileInstallationRoots roots{
        root / "etc",
        root / "udev",
        root / "systemd",
        root / "public"
    };
    btrfsbackup::config::NullConfigurationActivator activator;
    FakeBtrfsOperations btrfs;
    linux_config::install_profile(json::profile_from_json(profile_document(), root / "mounts"), roots, activator);
    const auto private_profile = root / "etc" / "profiles" / "default" / "profile.json";
    const auto public_profile = root / "public" / "default.json";
    const auto udev_rule = root / "udev" / "99-btrfs-backup-default.rules";
    const auto systemd_dropin = root / "systemd" / "btrfs-backup@default.service.d" / "target-mount.conf";
    const auto manifest = root / "etc" / "profiles" / "default" / "managed-artifacts.json";
    const auto mount_unit = root / "systemd" /
        json::load_json_file(manifest).at("mounts").at(0).at("unit").get<std::string>();
    const std::string unsupported = R"({"schemaVersion":4,"arbitrary":{"old":"fields"}})";
    test_helpers::write_file(private_profile, unsupported);
    test_helpers::write_file(public_profile, R"({"schemaVersion":4,"profileId":"default","name":"Old"})");
    const auto backup_data = root / "mounts" / "default" / "snapshots" / "preserved";
    test_helpers::write_file(backup_data, "backup-data");
    const auto profile_state = root / "state" / "profiles" / "default";
    const auto profile_history = root / "history" / "default";
    const auto profile_status = root / "status" / "default";
    test_helpers::write_file(profile_state / "last-success", "timestamp=2026-08-24T18:42:00+0000\n");
    test_helpers::write_file(profile_state / "checkpoint.json", "old-checkpoint");
    test_helpers::write_file(profile_history / "old.json", "old-history");
    test_helpers::write_file(profile_status / "current.json", "old-status");

    SystemProfileAdministrationBackend backend(
        {
            .etc_root = roots.etc_root,
            .udev_root = roots.udev_root,
            .systemd_root = roots.systemd_root,
            .public_root = roots.public_root,
            .state_root = root / "state",
            .status_root = root / "status",
            .history_root = root / "history",
        },
        root / "mounts",
        "/proc/self/mountinfo",
        btrfs,
        activator
    );
    const auto expected = backend.inspect_unsupported_profile(ProfileId{"default"});
    test_helpers::expect_true(
        "unsupported schema detected",
        expected.detected_schema_version == 4 && !expected.fingerprint.empty() &&
            expected.managed_artifact_manifest_fingerprint.has_value(),
        "unsupported profile identity was incomplete"
    );

    const std::string original_manifest = read_file(manifest);
    const auto saved_manifest = manifest.string() + ".saved";
    std::filesystem::rename(manifest, saved_manifest);
    std::filesystem::create_symlink(saved_manifest, manifest);
    try {
        static_cast<void>(backend.inspect_unsupported_profile(ProfileId{"default"}));
        test_helpers::fail("retirement manifest symlink", "symlink manifest was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
    std::filesystem::remove(manifest);
    std::filesystem::rename(saved_manifest, manifest);

    test_helpers::write_file(manifest, std::string(64U * 1024U + 1U, 'x'));
    try {
        static_cast<void>(backend.inspect_unsupported_profile(ProfileId{"default"}));
        test_helpers::fail("retirement manifest bound", "oversized manifest was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
    test_helpers::write_file(manifest, original_manifest);

    test_helpers::write_file(private_profile, R"({"schemaVersion":5})");
    try {
        backend.retire_unsupported_profile(expected);
        test_helpers::fail("retirement fingerprint race", "changed profile was retired");
    } catch (const btrfsbackup::CodedValidationError& error) {
        test_helpers::expect_true(
            "retirement fingerprint conflict",
            error.error_code == btrfsbackup::ErrorCode::ConfigurationChanged,
            "changed profile returned the wrong error"
        );
    }
    test_helpers::expect_true(
        "fingerprint conflict preserved profile",
        std::filesystem::is_regular_file(private_profile),
        "changed profile disappeared"
    );

    test_helpers::write_file(private_profile, unsupported);
    const auto manifest_expected = backend.inspect_unsupported_profile(ProfileId{"default"});
    test_helpers::write_file(manifest, original_manifest + " ");
    try {
        backend.retire_unsupported_profile(manifest_expected);
        test_helpers::fail("retirement manifest race", "changed manifest was accepted");
    } catch (const btrfsbackup::CodedValidationError& error) {
        test_helpers::expect_true(
            "retirement manifest conflict",
            error.error_code == btrfsbackup::ErrorCode::ConfigurationChanged,
            "changed manifest returned the wrong error"
        );
    }
    test_helpers::expect_true(
        "manifest conflict restored state",
        std::filesystem::is_regular_file(profile_state / "checkpoint.json"),
        "manifest conflict did not restore quarantined state"
    );
    test_helpers::write_file(manifest, original_manifest);
    backend.retire_unsupported_profile(backend.inspect_unsupported_profile(ProfileId{"default"}));
    for (const auto& artifact : {private_profile, public_profile, udev_rule, systemd_dropin, manifest, mount_unit}) {
        test_helpers::expect_true(
            "unsupported artifact removed",
            !std::filesystem::exists(artifact),
            "unsupported configuration artifact remains: " + artifact.string()
        );
    }
    test_helpers::expect_true(
        "backup data preserved",
        std::filesystem::is_regular_file(backup_data),
        "unsupported profile retirement removed backup data"
    );
    for (const auto& directory : {profile_state, profile_history, profile_status}) {
        test_helpers::expect_true(
            "retired profile state isolated",
            !std::filesystem::exists(directory),
            "retired profile state remains visible: " + directory.string()
        );
    }
    const auto retired_profile = root / "state" / "retired" / "default";
    std::vector<std::filesystem::path> retirements;
    for (const auto& entry : std::filesystem::directory_iterator(retired_profile))
        retirements.push_back(entry.path());
    test_helpers::expect_true(
        "single retirement quarantine",
        retirements.size() == 1,
        "retirement did not create one quarantine generation"
    );
    if (retirements.size() == 1) {
        test_helpers::expect_true(
            "state quarantined",
            read_file(retirements.front() / "state" / "checkpoint.json") == "old-checkpoint",
            "persistent profile state was not preserved in quarantine"
        );
        test_helpers::expect_true(
            "history quarantined",
            read_file(retirements.front() / "history" / "old.json") == "old-history",
            "profile history was not preserved in quarantine"
        );
    }
}

void test_unsupported_profile_retirement_rolls_back_as_one_transaction() {
    const auto root = test_helpers::test_root("profile-administration", "unsupported-rollback");
    const linux_config::ProfileInstallationRoots roots{
        root / "etc",
        root / "udev",
        root / "systemd",
        root / "public"
    };
    btrfsbackup::config::NullConfigurationActivator installer_activator;
    FakeBtrfsOperations btrfs;
    linux_config::install_profile(
        json::profile_from_json(profile_document(), root / "mounts"),
        roots,
        installer_activator
    );
    const auto private_profile = root / "etc" / "profiles" / "default" / "profile.json";
    const auto public_profile = root / "public" / "default.json";
    const auto udev_rule = root / "udev" / "99-btrfs-backup-default.rules";
    const auto systemd_dropin = root / "systemd" / "btrfs-backup@default.service.d" / "target-mount.conf";
    const auto manifest = root / "etc" / "profiles" / "default" / "managed-artifacts.json";
    const auto mount_unit = root / "systemd" /
        json::load_json_file(manifest).at("mounts").at(0).at("unit").get<std::string>();
    test_helpers::write_file(private_profile, R"({"schemaVersion":4,"legacy":true})");
    test_helpers::write_file(public_profile, R"({"schemaVersion":4,"profileId":"default"})");
    const auto profile_state = root / "state" / "profiles" / "default";
    const auto profile_history = root / "history" / "default";
    const auto profile_status = root / "status" / "default";
    test_helpers::write_file(profile_state / "checkpoint.json", "old-checkpoint");
    test_helpers::write_file(profile_history / "old.json", "old-history");
    test_helpers::write_file(profile_status / "current.json", "old-status");

    FailingActivator activator;
    SystemProfileAdministrationBackend backend(
        {
            .etc_root = roots.etc_root,
            .udev_root = roots.udev_root,
            .systemd_root = roots.systemd_root,
            .public_root = roots.public_root,
            .state_root = root / "state",
            .status_root = root / "status",
            .history_root = root / "history",
        },
        root / "mounts",
        "/proc/self/mountinfo",
        btrfs,
        activator
    );
    try {
        backend.retire_unsupported_profile(backend.inspect_unsupported_profile(ProfileId{"default"}));
        test_helpers::fail("retirement rollback", "activation failure was ignored");
    } catch (const linux_config::ConfigurationSaveError&) {
    }
    test_helpers::expect_true("retirement reactivated", activator.calls == 2, "rollback was not reactivated");
    for (const auto& artifact : {private_profile, public_profile, udev_rule, systemd_dropin, manifest, mount_unit}) {
        test_helpers::expect_true(
            "retirement rollback artifact",
            std::filesystem::is_regular_file(artifact),
            "retirement rollback did not restore " + artifact.string()
        );
    }
    test_helpers::expect_true(
        "retirement rollback state",
        read_file(profile_state / "checkpoint.json") == "old-checkpoint",
        "retirement rollback did not restore profile state"
    );
    test_helpers::expect_true(
        "retirement rollback history",
        read_file(profile_history / "old.json") == "old-history",
        "retirement rollback did not restore profile history"
    );
    test_helpers::expect_true(
        "retirement rollback status",
        read_file(profile_status / "current.json") == "old-status",
        "retirement rollback did not restore transient status"
    );
}

} // namespace

int main() {
    test_backend_preserves_secrets_and_hook_boundary();
    test_unsupported_profile_retirement_is_bounded_and_fingerprint_pinned();
    test_unsupported_profile_retirement_rolls_back_as_one_transaction();
    return test_helpers::finish("system profile administration backend tests");
}
