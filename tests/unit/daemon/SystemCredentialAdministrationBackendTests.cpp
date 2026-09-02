// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemCredentialAdministrationBackend.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <config/json/ProfileDocument.hpp>
#include <config/json/JsonIo.hpp>
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
    std::function<void()> after_remove;

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
        if (after_remove)
            after_remove();
    }
    fs::path active_device(const std::string&) override {
        return "/dev/test";
    }
    std::string format_luks2(const fs::path&, int) override {
        return std::string(luks_uuid);
    }
    void open_luks2(const fs::path&, const std::string&, int) override {
    }
    void open_luks2_read_only(const fs::path&, const std::string&, int) override {
    }
    void close(const std::string&) override {
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
        .trusted_owner = geteuid(),
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

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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

void test_removal_restores_quarantined_key_when_keyslot_removal_fails() {
    const auto root = test_helpers::test_root("credential-administration", "removal-keyslot-failure");
    const auto paths = roots(root);
    install_profile(paths);
    Cryptsetup cryptsetup;
    btrfsbackup::config::NullConfigurationActivator activator;
    control::SystemCredentialAdministrationBackend backend(paths, cryptsetup, activator);
    auto authorization = secret("authorization");
    auto credential = secret("credential");
    backend.add_key(ProfileId{"default"}, authorization.get(), credential.get(), "Recovery", false);
    auto removal_authorization = secret("authorization");
    cryptsetup.fail_remove = true;

    try {
        backend.remove_credential(ProfileId{"default"}, "slot-1", removal_authorization.get());
        test_helpers::fail("credential removal rollback", "removal succeeded");
    } catch (const control::CredentialRemovalFailure&) {
        test_helpers::fail("credential removal rollback", "complete rollback became partial");
    } catch (const ValidationError& error) {
        test_helpers::expect_contains("keyslot removal error", error.what(), "keyslot rollback failed");
    }

    test_helpers::expect_true(
        "quarantined key restored",
        fs::exists(paths.key_root / "default-slot-1.key"),
        "key file was not restored after keyslot removal failed"
    );
    const auto listed = backend.list_credentials(ProfileId{"default"});
    test_helpers::expect_true(
        "credential metadata preserved",
        std::ranges::find(listed, std::string("slot-1"), &control::TargetCredential::id) != listed.end(),
        "credential metadata changed before keyslot removal committed"
    );
}

void test_metadata_commit_failure_reports_irreversible_removal() {
    const auto root = test_helpers::test_root("credential-administration", "removal-metadata-failure");
    const auto paths = roots(root);
    install_profile(paths);
    Cryptsetup cryptsetup;
    btrfsbackup::config::NullConfigurationActivator activator;
    control::SystemCredentialAdministrationBackend backend(paths, cryptsetup, activator);
    auto authorization = secret("authorization");
    auto credential = secret("credential");
    backend.add_key(ProfileId{"default"}, authorization.get(), credential.get(), "Recovery", false);
    auto removal_authorization = secret("authorization");
    cryptsetup.after_remove = [&] {
        std::error_code error;
        fs::remove_all(paths.metadata_root, error);
        test_helpers::write_file(paths.metadata_root, "not a directory");
    };

    try {
        backend.remove_credential(ProfileId{"default"}, "slot-1", removal_authorization.get());
        test_helpers::fail("credential removal metadata commit", "removal succeeded");
    } catch (const control::CredentialRemovalFailure& error) {
        test_helpers::expect_true(
            "metadata failure stage",
            error.failed_stage == control::CredentialRemovalStage::CommitMetadata,
            "metadata commit stage was lost"
        );
        test_helpers::expect_true(
            "irreversible removal state",
            error.key_file_quarantined && !error.key_file_restored && error.keyslot_state_known &&
                error.keyslot_removed && !error.metadata_committed,
            "irreversible credential removal state is inaccurate"
        );
    }
    test_helpers::expect_true(
        "keyslot removal retained",
        cryptsetup.slots == std::vector<int>{0},
        "removed keyslot reappeared after metadata commit failed"
    );
}

void test_quarantine_cleanup_failure_is_a_warning_after_success() {
    const auto root = test_helpers::test_root("credential-administration", "removal-cleanup-warning");
    const auto paths = roots(root);
    install_profile(paths);
    Cryptsetup cryptsetup;
    btrfsbackup::config::NullConfigurationActivator activator;
    control::SystemCredentialAdministrationBackend backend(paths, cryptsetup, activator);
    auto authorization = secret("authorization");
    auto credential = secret("credential");
    backend.add_key(ProfileId{"default"}, authorization.get(), credential.get(), "Recovery", false);
    auto removal_authorization = secret("authorization");
    const fs::path quarantine = paths.key_root / ".quarantine";
    cryptsetup.after_remove = [&] {
        const auto entry = *fs::directory_iterator(quarantine);
        fs::remove(entry.path());
        fs::create_directories(entry.path());
        test_helpers::write_file(entry.path() / "blocker", "prevent cleanup");
    };

    std::ostringstream warnings;
    std::streambuf* previous = std::cerr.rdbuf(warnings.rdbuf());
    try {
        backend.remove_credential(ProfileId{"default"}, "slot-1", removal_authorization.get());
    } catch (...) {
        std::cerr.rdbuf(previous);
        throw;
    }
    std::cerr.rdbuf(previous);

    test_helpers::expect_contains(
        "quarantine cleanup warning",
        warnings.str(),
        "credential removal succeeded but quarantined key file could not be deleted"
    );
    test_helpers::expect_true(
        "credential removal committed",
        cryptsetup.slots == std::vector<int>{0} &&
            backend.list_credentials(ProfileId{"default"}).size() == 1,
        "cleanup warning changed the successful removal result"
    );
    std::error_code cleanup_error;
    fs::remove_all(quarantine, cleanup_error);
}

void test_metadata_rejects_key_file_outside_trusted_root() {
    const auto root = test_helpers::test_root("credential-administration", "external-key-file");
    const auto paths = roots(root);
    install_profile(paths);
    Cryptsetup cryptsetup;
    cryptsetup.slots.push_back(1);
    btrfsbackup::config::NullConfigurationActivator activator;
    control::SystemCredentialAdministrationBackend backend(paths, cryptsetup, activator);
    const fs::path external_key = (root / "outside.key").lexically_normal();
    test_helpers::write_file(
        paths.metadata_root / (std::string(luks_uuid) + ".json"),
        btrfsbackup::config::json::dump_json({
            {"schemaVersion", 1},
            {"luksUuid", luks_uuid},
            {"credentials",
             btrfsbackup::config::json::Json::array({{
                 {"id", "slot-1"},
                 {"label", "External"},
                 {"type", "keyFile"},
                 {"keyslot", 1},
                 {"keyFile", external_key.string()},
                 {"automatic", false},
             }})},
        })
    );

    try {
        static_cast<void>(backend.list_credentials(ProfileId{"default"}));
        test_helpers::fail("external key metadata", "external key path was accepted");
    } catch (const ValidationError& error) {
        test_helpers::expect_contains(
            "external key metadata error",
            error.what(),
            "managed credential entry is invalid"
        );
    }
}

void test_failed_install_does_not_remove_existing_key_file() {
    const auto root = test_helpers::test_root("credential-administration", "existing-key-file");
    const auto paths = roots(root);
    install_profile(paths);
    fs::create_directories(paths.key_root);
    chmod(paths.key_root.c_str(), 0700);
    const fs::path existing_key = paths.key_root / "default-slot-1.key";
    test_helpers::write_file(existing_key, "existing key material");
    Cryptsetup cryptsetup;
    btrfsbackup::config::NullConfigurationActivator activator;
    control::SystemCredentialAdministrationBackend backend(paths, cryptsetup, activator);
    auto authorization = secret("authorization");
    auto credential = secret("credential");

    try {
        backend.add_key(ProfileId{"default"}, authorization.get(), credential.get(), "Recovery", false);
        test_helpers::fail("existing key no-replace", "credential installation succeeded");
    } catch (const ValidationError&) {
    }

    test_helpers::expect_eq(
        "existing key preserved",
        read_file(existing_key),
        std::string("existing key material")
    );
    test_helpers::expect_true(
        "keyslot rolled back after collision",
        cryptsetup.slots == std::vector<int>{0},
        "new keyslot remained after key filename collision"
    );
}

} // namespace

int main() {
    test_complete_rollback_preserves_primary_failure();
    test_keyslot_rollback_failure_is_typed();
    test_metadata_and_profile_rollback_failures_are_typed();
    test_removal_restores_quarantined_key_when_keyslot_removal_fails();
    test_metadata_commit_failure_reports_irreversible_removal();
    test_quarantine_cleanup_failure_is_a_warning_after_success();
    test_metadata_rejects_key_file_outside_trusted_root();
    test_failed_install_does_not_remove_existing_key_file();
    return test_helpers::finish("system credential administration backend tests");
}
