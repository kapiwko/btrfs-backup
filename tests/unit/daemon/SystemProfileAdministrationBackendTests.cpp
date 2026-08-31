// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemProfileAdministrationBackend.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include <config/json/JsonIo.hpp>
#include <config/json/ProfileDocument.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <platform/linux/config/ProfileService.hpp>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::ProfileId;
using btrfsbackup::daemon::control::SystemProfileAdministrationBackend;
using btrfsbackup::daemon::dbus::ManagerOperationError;
namespace json = btrfsbackup::config::json;
namespace linux_config = btrfsbackup::platform::linux::config;

class FakeBtrfsOperations final : public btrfsbackup::backup::IBtrfsOperations {
  public:
    bool is_subvolume(const std::filesystem::path&) override { return true; }
    std::optional<btrfsbackup::backup::SnapshotMetadata> read_snapshot_metadata(const std::filesystem::path&) override {
        return std::nullopt;
    }
    void create_readonly_snapshot(const std::filesystem::path&, const std::filesystem::path&) override {}
    void delete_subvolume(const std::filesystem::path&) override {}
};

json::Json profile_document() {
    return {
        {"schemaVersion", 4}, {"profileId", "default"}, {"name", "Default"}, {"enabled", true},
        {"target", {
            {"device", "/dev/null"}, {"luksUuid", "11111111-2222-3333-4444-555555555555"},
            {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"}, {"partitionUuid", ""},
            {"serial", ""}, {"mapperName", "backupdisk"},
            {"activation", {{"mode", "keyFile"}, {"keyFile", "/etc/btrfs-backup/key.secret"}}},
        }},
        {"paths", {{"remoteRoot", "/snapshots"}, {"incomingRoot", "/.incoming"}}},
        {"settings", json::Json::object()},
        {"hooks", {{"beforeSnapshot", json::Json::array()}, {"afterSnapshot", json::Json::array()}}},
        {"sources", json::Json::array({{
            {"id", "home"}, {"name", "Home"}, {"enabled", true}, {"subvolume", "/home"},
            {"localSnapshotDir", "/.snapshots/home"}, {"remoteSubdir", "home"},
            {"remoteRetention", 2}, {"localRetention", 2},
        }})},
    };
}

void test_backend_preserves_secrets_and_hook_boundary() {
    const auto root = test_helpers::test_root("profile-administration", "backend");
    const linux_config::ProfileInstallationRoots roots{
        root / "etc", root / "udev", root / "systemd", root / "public"
    };
    btrfsbackup::config::NullConfigurationActivator activator;
    FakeBtrfsOperations btrfs;
    const auto initial = json::profile_from_json(profile_document(), root / "mounts");
    linux_config::install_profile(initial, roots, activator);
    test_helpers::write_file(root / "etc" / "key.secret", "TOP-SECRET-KEY-CONTENTS");

    SystemProfileAdministrationBackend backend(
        {roots.etc_root, roots.udev_root, roots.systemd_root, roots.public_root},
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

    auto changed = json::Json::parse(current->document);
    changed["hooks"]["beforeSnapshot"] = json::Json::array({{
        {"type", "program"}, {"program", "/etc/btrfs-backup/hooks.d/test"},
        {"arguments", json::Json::array()}, {"timeoutSeconds", 30},
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

} // namespace

int main() {
    test_backend_preserves_secrets_and_hook_boundary();
    return test_helpers::finish("system profile administration backend tests");
}
