// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemCredentialAdministrationBackend.hpp>

#include <algorithm>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <config/json/ProfileDocument.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <core/Errors.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>

#include "support/TestHelpers.hpp"

namespace {

namespace fs = std::filesystem;
namespace control = btrfsbackup::daemon::control;
namespace linux_config = btrfsbackup::platform::linux::config;
using btrfsbackup::ProfileId;
using btrfsbackup::ValidationError;
using btrfsbackup::platform::linux::OwnedFileDescriptor;

constexpr std::string_view luks_uuid = "11111111-2222-3333-4444-555555555555";

class Cryptsetup final : public btrfsbackup::platform::linux::storage::ICryptsetupOperations {
  public:
    std::vector<int> slots{0};
    bool fail_test = false;
    bool fail_remove = false;

    btrfsbackup::platform::linux::storage::LuksHeader inspect_luks2(const fs::path&) override {
        return {std::string(luks_uuid), slots};
    }
    void add_key(const fs::path&, int, int) override {
        slots.push_back(1);
    }
    void test_key(const fs::path&, int) override {
        if (fail_test)
            throw ValidationError("new credential verification failed");
    }
    void remove_keyslot(const fs::path&, int keyslot, int) override {
        if (fail_remove)
            throw ValidationError("keyslot rollback failed");
        std::erase(slots, keyslot);
    }
};

class SabotagingActivator final : public btrfsbackup::config::IConfigurationActivator {
  public:
    explicit SabotagingActivator(fs::path metadata_root)
        : metadata_root_(std::move(metadata_root)) {
    }

    void activate() override {
        std::error_code error;
        fs::remove_all(metadata_root_, error);
        test_helpers::write_file(metadata_root_, "not a directory");
        throw ValidationError("configuration activation failed");
    }

  private:
    fs::path metadata_root_;
};

control::CredentialAdministrationRoots roots(const fs::path& root) {
    return {
        .config_root = root / "etc",
        .metadata_root = root / "etc/credentials",
        .key_root = root / "etc/keys",
        .lock_root = root / "run/locks",
        .udev_root = root / "udev",
        .systemd_root = root / "systemd",
        .public_root = root / "public",
    };
}

void install_profile(const control::CredentialAdministrationRoots& paths) {
    const btrfsbackup::config::json::Json document{
        {"schemaVersion", 4},
        {"profileId", "default"},
        {"name", "Default"},
        {"enabled", false},
        {"target",
         {
             {"device", "/dev/test"},
             {"luksUuid", luks_uuid},
             {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
             {"partitionUuid", ""},
             {"serial", ""},
             {"mapperName", "backupdisk"},
             {"activation", {{"mode", "askPassword"}}},
         }},
        {"paths", {{"remoteRoot", "/snapshots"}, {"incomingRoot", "/.incoming"}}},
        {"settings", btrfsbackup::config::json::Json::object()},
        {"hooks",
         {{"beforeSnapshot", btrfsbackup::config::json::Json::array()},
          {"afterSnapshot", btrfsbackup::config::json::Json::array()}}},
        {"sources",
         btrfsbackup::config::json::Json::array({{
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
    btrfsbackup::config::NullConfigurationActivator activator;
    linux_config::install_profile(
        btrfsbackup::config::json::profile_from_json(document),
        {paths.config_root, paths.udev_root, paths.systemd_root, paths.public_root},
        activator
    );
}

OwnedFileDescriptor secret(std::string_view value) {
    return btrfsbackup::platform::linux::filesystem::create_sealed_secret_file(
        std::as_bytes(std::span(value.data(), value.size()))
    );
}

void test_complete_rollback_preserves_primary_failure() {
    const auto root = test_helpers::test_root("credential-administration", "complete-rollback");
    const auto paths = roots(root);
    install_profile(paths);
    Cryptsetup cryptsetup;
    cryptsetup.fail_test = true;
    btrfsbackup::config::NullConfigurationActivator activator;
    control::SystemCredentialAdministrationBackend backend(paths, cryptsetup, activator);
    auto authorization = secret("authorization");
    auto credential = secret("credential");

    try {
        backend.add_passphrase(ProfileId{"default"}, authorization.get(), credential.get(), "Recovery");
        test_helpers::fail("complete credential rollback", "mutation succeeded");
    } catch (const control::CredentialMutationFailure&) {
        test_helpers::fail("complete credential rollback", "complete rollback became partial");
    } catch (const ValidationError& error) {
        test_helpers::expect_contains(
            "primary credential failure",
            error.what(),
            "verification failed"
        );
    }
    test_helpers::expect_true(
        "complete keyslot rollback",
        cryptsetup.slots == std::vector<int>{0},
        "new keyslot remained after complete rollback"
    );
}

void test_keyslot_rollback_failure_is_typed() {
    const auto root = test_helpers::test_root("credential-administration", "keyslot-rollback");
    const auto paths = roots(root);
    install_profile(paths);
    Cryptsetup cryptsetup;
    cryptsetup.fail_test = true;
    cryptsetup.fail_remove = true;
    btrfsbackup::config::NullConfigurationActivator activator;
    control::SystemCredentialAdministrationBackend backend(paths, cryptsetup, activator);
    auto authorization = secret("authorization");
    auto credential = secret("credential");

    try {
        backend.add_passphrase(ProfileId{"default"}, authorization.get(), credential.get(), "Recovery");
        test_helpers::fail("partial keyslot rollback", "mutation succeeded");
    } catch (const control::CredentialMutationFailure& error) {
        test_helpers::expect_true(
            "credential rollback error code",
            error.error_code == btrfsbackup::ErrorCode::CredentialMutationRollbackIncomplete,
            "partial credential rollback used the wrong stable error code"
        );
        test_helpers::expect_true(
            "keyslot failure stage",
            error.failed_stage == control::CredentialMutationStage::VerifyKey,
            "failed mutation stage was lost"
        );
        test_helpers::expect_true(
            "keyslot partial state",
            error.keyslot_added && !error.keyslot_rollback_succeeded &&
                error.key_file_removed && error.metadata_restored && error.profile_restored,
            "partial keyslot rollback state is inaccurate"
        );
        test_helpers::expect_contains(
            "keyslot primary error",
            error.primary_error,
            "verification failed"
        );
    }
}

void test_metadata_and_profile_rollback_failures_are_typed() {
    const auto root = test_helpers::test_root("credential-administration", "metadata-rollback");
    const auto paths = roots(root);
    install_profile(paths);
    Cryptsetup cryptsetup;
    SabotagingActivator activator(paths.metadata_root);
    control::SystemCredentialAdministrationBackend backend(paths, cryptsetup, activator);
    auto authorization = secret("authorization");
    auto credential = secret("credential");

    try {
        backend.add_key(ProfileId{"default"}, authorization.get(), credential.get(), "Automatic", true);
        test_helpers::fail("metadata credential rollback", "mutation succeeded");
    } catch (const control::CredentialMutationFailure& error) {
        test_helpers::expect_true(
            "profile failure stage",
            error.failed_stage == control::CredentialMutationStage::PublishProfile,
            "profile publication stage was lost"
        );
        test_helpers::expect_true(
            "metadata partial state",
            error.keyslot_added && error.keyslot_rollback_succeeded &&
                error.key_file_removed && !error.metadata_restored && !error.profile_restored,
            "metadata or profile rollback failure was hidden"
        );
    }
    test_helpers::expect_true(
        "partial key file removed",
        !fs::exists(paths.key_root / "default-slot-1.key"),
        "key file remained after rollback"
    );
}

} // namespace

int main() {
    test_complete_rollback_preserves_primary_failure();
    test_keyslot_rollback_failure_is_typed();
    test_metadata_and_profile_rollback_failures_are_typed();
    return test_helpers::finish("system credential administration backend tests");
}
