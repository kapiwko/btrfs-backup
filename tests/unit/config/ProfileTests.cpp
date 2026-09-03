// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <unistd.h>

#include <core/Errors.hpp>
#include <config/json/Json.hpp>
#include <config/json/JsonIo.hpp>
#include <config/domain/Profile.hpp>
#include <config/json/ProfileDocument.hpp>
#include <config/ProfileArtifactRenderer.hpp>
#include <platform/linux/config/ProfileArtifactIo.hpp>
#include <platform/linux/config/ProfileConfigurationTransaction.hpp>
#include <config/ProfileFingerprint.hpp>
#include <platform/linux/config/ProfileInstaller.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/config/ProfileRuntimePolicy.hpp>
#include <config/ProfileRender.hpp>
#include <platform/linux/filesystem/FileIo.hpp>
#include <platform/linux/filesystem/FileLock.hpp>

namespace fs = std::filesystem;

namespace {

using btrfsbackup::config::json::Json;
using btrfsbackup::ValidationError;

int failures = 0;

class FakeConfigurationActivator final : public btrfsbackup::config::IConfigurationActivator {
  public:
    explicit FakeConfigurationActivator(std::function<void()> activate)
        : activate_(std::move(activate)) {
    }

    void activate() override {
        activate_();
    }

  private:
    std::function<void()> activate_;
};

void fail(const std::string& name, const std::string& message) {
    ++failures;
    std::cerr << "not ok - " << name << ": " << message << '\n';
}

void expect_true(const std::string& name, bool condition, const std::string& message) {
    if (!condition) {
        fail(name, message);
    }
}

void expect_validation_error(const std::string& name, const std::function<void()>& fn, const std::string& expected) {
    try {
        fn();
        fail(name, "expected ValidationError");
    } catch (const ValidationError& exc) {
        std::string message = exc.what();
        if (message.find(expected) == std::string::npos) {
            fail(name, "unexpected error: " + message);
        }
    } catch (const std::exception& exc) {
        fail(name, std::string("unexpected exception: ") + exc.what());
    }
}

void install_test_profile_transactionally(
    const btrfsbackup::config::Profile& profile,
    const fs::path& etc_root,
    const fs::path& udev_root,
    const fs::path& systemd_root,
    const fs::path& public_root,
    const std::function<void()>& activate = {}
) {
    btrfsbackup::config::ProfileArtifactRenderer renderer(btrfsbackup::platform::linux::config::generate_configuration_generation);
    const btrfsbackup::config::ProfileArtifactRoots roots{
        .etc_root = etc_root,
        .udev_root = udev_root,
        .systemd_root = systemd_root,
        .public_root = public_root,
    };
    if (activate) {
        FakeConfigurationActivator activator(activate);
        btrfsbackup::platform::linux::config::ProfileInstaller installer(renderer, activator);
        installer.install_profile_transactionally(profile, roots);
    } else {
        btrfsbackup::config::NullConfigurationActivator activator;
        btrfsbackup::platform::linux::config::ProfileInstaller installer(renderer, activator);
        installer.install_profile_transactionally(profile, roots);
    }
}

btrfsbackup::config::json::Json valid_profile() {
    return {
        {"schemaVersion", 4},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {{"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"}, {"luksUuid", "11111111-2222-3333-4444-555555555555"}, {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"}, {"partitionUuid", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"}, {"serial", "SERIAL_123"}, {"mapperName", "backupdisk"}, {"activation", {{"mode", "askPassword"}}}}},
        {"paths", {{"remoteRoot", "/mnt/btrfs-backup/default/snapshots"}, {"incomingRoot", "/mnt/btrfs-backup/default/.incoming"}}},
        {"settings", {{"dailyLimit", true}, {"incrementalRequired", true}, {"keepFailedLocalSnapshot", false}, {"autoEject", true}, {"remoteRetention", 30}, {"localRetention", 20}, {"minimumTargetFreeBytes", 5368709120LL}, {"minimumLocalFreeBytes", 1073741824LL}}},
        {"sources", btrfsbackup::config::json::Json::array({{{"id", "home"}, {"name", "Home"}, {"enabled", true}, {"subvolume", "/home"}, {"localSnapshotDir", "/.snapshots/btrfs-backup/home"}, {"remoteSubdir", "home"}, {"remoteRetention", 30}, {"localRetention", 20}}})}
    };
}

fs::path test_root() {
    fs::path root = fs::temp_directory_path() / ("btrfsbackup-cpp-tests-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

std::string read_text(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int mode_of(const fs::path& path) {
    struct stat info{};
    return stat(path.c_str(), &info) == 0 ? info.st_mode & 0777 : -1;
}

void test_profile_repository_loads_profile_and_fingerprint_from_one_read() {
    const fs::path config_root = "/unused/test/config";
    const fs::path profile_path = config_root / "profiles" / "default" / "profile.json";
    btrfsbackup::config::json::Json installed = valid_profile();
    installed["configurationGeneration"] = "0123456789abcdef0123456789abcdef";
    const std::string bytes = btrfsbackup::config::json::dump_json(installed);
    int reads = 0;
    btrfsbackup::platform::linux::config::FileProfileRepository repository(
        config_root,
        btrfsbackup::config::ApplicationConfig::defaults(),
        [&](const fs::path& requested_path) {
            ++reads;
            expect_true("atomic profile path", requested_path == profile_path, "repository read wrong profile path");
            return bytes;
        }
    );

    const btrfsbackup::config::LoadedProfile loaded = repository.get(btrfsbackup::ProfileId{"default"});
    const std::string expected_fingerprint = btrfsbackup::config::compute_config_fingerprint_from_bytes(
        btrfsbackup::config::current_configuration_fingerprint_version,
        profile_path,
        bytes
    );

    expect_true("atomic profile read count", reads == 1, "profile was read more than once");
    expect_true("atomic profile id", loaded.profile.id == btrfsbackup::ProfileId{"default"}, "wrong profile loaded");
    expect_true("atomic profile fingerprint", loaded.fingerprint.value() == expected_fingerprint, "fingerprint did not use loaded bytes");
    expect_true(
        "atomic profile generation",
        loaded.generation.value() == "0123456789abcdef0123456789abcdef",
        "wrong profile generation"
    );
}

void test_profile_repository_rejects_missing_generation() {
    const std::string bytes = btrfsbackup::config::json::dump_json(valid_profile());
    btrfsbackup::platform::linux::config::FileProfileRepository repository(
        "/unused/test/config",
        btrfsbackup::config::ApplicationConfig::defaults(),
        [&](const fs::path&) { return bytes; }
    );
    expect_validation_error(
        "installed profile generation",
        [&] { (void)repository.get(btrfsbackup::ProfileId{"default"}); },
        "configurationGeneration is required"
    );
}

void test_rejects_bad_uuid() {
    btrfsbackup::config::json::Json profile = valid_profile();
    profile["target"]["luksUuid"] = "bad";
    expect_validation_error("bad uuid", [&] { btrfsbackup::config::json::normalize_profile(profile); }, "target.luksUuid");
}

void test_rejects_bad_configuration_generation() {
    btrfsbackup::config::json::Json profile = valid_profile();
    profile["configurationGeneration"] = "NOT-A-GENERATION";
    expect_validation_error(
        "bad configuration generation",
        [&] { (void)btrfsbackup::config::json::normalize_profile(profile); },
        "configurationGeneration"
    );
}

void test_rejects_missing_btrfs_uuid() {
    btrfsbackup::config::json::Json profile = valid_profile();
    profile["target"].erase("btrfsUuid");
    expect_validation_error("missing btrfs uuid", [&] { btrfsbackup::config::json::normalize_profile(profile); }, "btrfsUuid");
}

void test_rejects_non_dev_target() {
    btrfsbackup::config::json::Json profile = valid_profile();
    profile["target"]["device"] = "/tmp/not-a-device";
    expect_validation_error("non-dev target", [&] { btrfsbackup::config::json::normalize_profile(profile); }, "target.device");
}

void test_rejects_nested_roots() {
    btrfsbackup::config::json::Json profile = valid_profile();
    profile["paths"]["incomingRoot"] = "/mnt/btrfs-backup/default/snapshots/.incoming";
    expect_validation_error("nested roots", [&] { btrfsbackup::config::json::normalize_profile(profile); }, "remoteRoot and paths.incomingRoot");
}

void test_target_activation_is_structured() {
    btrfsbackup::config::json::Json key_file = valid_profile();
    key_file["target"]["activation"] = {
        {"mode", "keyFile"},
        {"keyFile", "/root/keys/backupdisk.key"},
    };
    const btrfsbackup::config::Profile key_file_profile =
        btrfsbackup::config::json::profile_from_json(key_file);
    expect_true(
        "activation key file mode",
        std::holds_alternative<btrfsbackup::config::KeyFileActivation>(key_file_profile.target.activation),
        "key file mode was not loaded"
    );
    const auto& activation = std::get<btrfsbackup::config::KeyFileActivation>(key_file_profile.target.activation);
    expect_true(
        "activation key file path",
        activation.key_file.value() == "/root/keys/backupdisk.key",
        "key file path was not loaded"
    );

    btrfsbackup::config::json::Json invalid = valid_profile();
    invalid["target"]["activation"] = {{"mode", "keyFile"}, {"keyFile", "relative.key"}};
    expect_validation_error(
        "activation absolute key file",
        [&] { (void)btrfsbackup::config::json::normalize_profile(invalid); },
        "absolute path"
    );

    invalid = valid_profile();
    invalid["target"]["activation"] = {
        {"mode", "askPassword"},
        {"keyFile", "/root/keys/backupdisk.key"},
    };
    expect_validation_error(
        "activation mode-specific field",
        [&] { (void)btrfsbackup::config::json::normalize_profile(invalid); },
        "only valid in keyFile mode"
    );
}

void test_profile_round_trips_normalized_json() {
    const btrfsbackup::config::json::ProfileDocument document =
        btrfsbackup::config::json::normalize_profile_document(valid_profile());
    const btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_document(document);
    const btrfsbackup::config::json::ProfileDocument round_trip = btrfsbackup::config::json::profile_to_document(profile);

    expect_true("profile model id", profile.id == btrfsbackup::ProfileId{"default"}, "wrong profile id");
    expect_true("profile model source", profile.sources.size() == 1 && profile.sources.at(0).id == btrfsbackup::SourceId{"home"}, "wrong profile source");
    expect_true("profile model round trip", round_trip.value == document.value, "typed profile did not preserve normalized document");
}

void test_invalid_profile_document_does_not_create_profile() {
    const btrfsbackup::config::json::ProfileDocument invalid{btrfsbackup::config::json::Json::object()};
    expect_validation_error(
        "invalid profile document",
        [&] { (void)btrfsbackup::config::json::profile_from_document(invalid); },
        "schemaVersion"
    );
}

void test_profile_rejects_old_schema_versions() {
    for (const int version : {1, 2, 3}) {
        btrfsbackup::config::json::Json old = valid_profile();
        old["schemaVersion"] = version;
        expect_validation_error(
            "old profile schema",
            [&] { (void)btrfsbackup::config::json::normalize_profile(old); },
            "schemaVersion must be 4"
        );
    }
}

void test_profile_rejects_removed_sources_directory() {
    btrfsbackup::config::json::Json profile = valid_profile();
    profile["paths"]["sourcesDir"] = "/etc/btrfs-backup/profiles/default/sources.d";
    expect_validation_error(
        "removed sources directory",
        [&] { (void)btrfsbackup::config::json::normalize_profile(profile); },
        "paths.sourcesDir is not supported"
    );
}

void test_profile_rejects_system_path_overrides() {
    btrfsbackup::config::json::Json current = valid_profile();
    current["paths"]["statusRoot"] = "/etc";
    expect_validation_error(
        "current system path",
        [&] { btrfsbackup::config::json::normalize_profile(current); },
        "paths.statusRoot is not supported"
    );
}

void test_mount_point_is_application_controlled() {
    btrfsbackup::config::json::Json raw = valid_profile();
    raw["target"]["mountPoint"] = "/home/alice/backup";
    expect_validation_error("profile mount point rejected", [&] { (void)btrfsbackup::config::json::normalize_profile(raw); }, "not supported");

    btrfsbackup::config::json::Json custom = valid_profile();
    custom["paths"] = btrfsbackup::config::json::Json::object();
    btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(custom, "/srv/backup-targets");
    expect_true(
        "custom mount root",
        profile.target.mount_point == "/srv/backup-targets/default",
        "profile mount point was not derived from the application root"
    );
}

void test_profile_rejects_removed_notifications() {
    btrfsbackup::config::json::Json raw = valid_profile();
    raw["notifications"] = {{"enabled", true}};

    expect_validation_error(
        "removed notifications",
        [&] { btrfsbackup::config::json::normalize_profile(raw); },
        "profile.notifications is not supported"
    );
}

void test_profile_hooks_round_trip_as_explicit_program_arguments() {
    btrfsbackup::config::json::Json raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", btrfsbackup::config::json::Json::array({{{"type", "program"}, {"program", "/etc/btrfs-backup/hooks.d/prepare-postgresql-backup"}, {"arguments", btrfsbackup::config::json::Json::array({"--mode", "snapshot"})}, {"timeoutSeconds", 45}}})},
        {"afterSnapshot", btrfsbackup::config::json::Json::array({{{"type", "program"}, {"program", "/etc/btrfs-backup/hooks.d/resume-postgresql"}, {"arguments", btrfsbackup::config::json::Json::array()}, {"timeoutSeconds", 30}}})}
    };

    btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(raw);
    btrfsbackup::config::json::Json round_trip = btrfsbackup::config::json::profile_to_json(profile);

    expect_true("before hook count", profile.hooks.before_snapshot.size() == 1, "wrong before hook count");
    expect_true("before hook program", profile.hooks.before_snapshot.at(0).program == "/etc/btrfs-backup/hooks.d/prepare-postgresql-backup", "wrong hook program");
    expect_true("before hook arg", profile.hooks.before_snapshot.at(0).arguments.at(1) == "snapshot", "wrong hook argument");
    expect_true("before hook timeout", profile.hooks.before_snapshot.at(0).timeout == std::chrono::seconds{45}, "wrong hook timeout");
    expect_true("after hook count", profile.hooks.after_snapshot.size() == 1, "wrong after hook count");
    expect_true("after hook timeout", profile.hooks.after_snapshot.at(0).timeout == std::chrono::seconds{30}, "wrong after hook timeout");
    expect_true("hook round trip", round_trip == btrfsbackup::config::json::normalize_profile(raw), "hook JSON did not round trip");
}

void test_profile_rejects_unsafe_hook_shape() {
    btrfsbackup::config::json::Json raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", btrfsbackup::config::json::Json::array({{{"type", "program"}, {"program", "/etc/btrfs-backup/hooks.d/prepare"}, {"arguments", btrfsbackup::config::json::Json::array()}}})}
    };
    expect_validation_error("hook timeout required", [&] { btrfsbackup::config::json::normalize_profile(raw); }, "timeoutSeconds is required");

    raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", btrfsbackup::config::json::Json::array({{{"type", "shell"}, {"program", "/etc/btrfs-backup/hooks.d/prepare"}, {"arguments", btrfsbackup::config::json::Json::array()}}})}
    };
    expect_validation_error("hook type", [&] { btrfsbackup::config::json::normalize_profile(raw); }, "type must be program");

    raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", btrfsbackup::config::json::Json::array({{{"type", "program"}, {"program", "prepare"}, {"arguments", btrfsbackup::config::json::Json::array()}, {"timeoutSeconds", 30}}})}
    };
    expect_validation_error("hook program absolute", [&] { btrfsbackup::config::json::normalize_profile(raw); }, "absolute path");

    raw["hooks"]["beforeSnapshot"][0]["program"] = "/home/kamil/bin/prepare";
    expect_validation_error(
        "hook program outside trusted directory",
        [&] {
            const btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(raw);
            btrfsbackup::platform::linux::config::validate_profile_runtime_policy(profile);
        },
        "must be a direct child of /etc/btrfs-backup/hooks.d"
    );

    raw["hooks"]["beforeSnapshot"][0]["program"] = "/etc/btrfs-backup/hooks.d/postgresql/prepare";
    expect_validation_error(
        "hook program nested directory",
        [&] {
            const btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(raw);
            btrfsbackup::platform::linux::config::validate_profile_runtime_policy(profile);
        },
        "must be a direct child of /etc/btrfs-backup/hooks.d"
    );

    raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", btrfsbackup::config::json::Json::array({{{"type", "program"}, {"program", "/etc/btrfs-backup/hooks.d/prepare"}, {"arguments", btrfsbackup::config::json::Json::array()}, {"timeoutSeconds", 0}}})}
    };
    expect_validation_error("hook timeout positive", [&] { btrfsbackup::config::json::normalize_profile(raw); }, "outside the supported range");
}

void test_profile_artifact_renderer() {
    fs::path root = test_root();
    btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(valid_profile());
    const std::string generation = "0123456789abcdef0123456789abcdef";
    btrfsbackup::config::ProfileArtifactRenderer renderer([&] {
        return btrfsbackup::config::ConfigurationGeneration{generation};
    });
    const fs::path rendered_root = root / "rendered";
    const btrfsbackup::config::RenderedProfileArtifacts rendered = renderer.render_profile_artifacts(
        profile,
        btrfsbackup::config::profile_artifact_roots(rendered_root)
    );

    expect_true(
        "renderer has no filesystem side effects",
        !fs::exists(rendered_root),
        "rendering wrote artifacts to disk"
    );
    expect_true("renderer artifact count", rendered.artifacts.size() == 6, "unexpected artifact count");
    expect_true(
        "renderer deterministic generation",
        rendered.profile.configuration_generation.value() == generation,
        "injected generation was not used"
    );
    const auto artifact = [&rendered](btrfsbackup::config::ProfileArtifactKind kind) -> const btrfsbackup::config::ProfileArtifact& {
        const auto found = std::find_if(
            rendered.artifacts.begin(),
            rendered.artifacts.end(),
            [kind](const btrfsbackup::config::ProfileArtifact& candidate) {
                return candidate.kind == kind;
            }
        );
        if (found == rendered.artifacts.end()) {
            throw std::runtime_error("missing rendered profile artifact");
        }
        return *found;
    };
    expect_true(
        "private profile permissions",
        artifact(btrfsbackup::config::ProfileArtifactKind::PrivateProfile).permissions ==
            (fs::perms::owner_read | fs::perms::owner_write),
        "private profile should be 0600"
    );
    for (const auto kind : {
             btrfsbackup::config::ProfileArtifactKind::UdevRule,
             btrfsbackup::config::ProfileArtifactKind::SystemdMountDependency,
             btrfsbackup::config::ProfileArtifactKind::NativeTargetMount,
             btrfsbackup::config::ProfileArtifactKind::PublicProfile,
         }) {
        expect_true(
            "public artifact permissions",
            artifact(kind).permissions ==
                (fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read),
            "public artifact should be 0644"
        );
    }
    btrfsbackup::platform::linux::config::write_profile_artifacts(rendered);

    expect_true("typed tree profile env", !fs::exists(root / "rendered" / "etc" / "btrfs-backup" / "profiles.d" / "default.env"), "profile env should not be rendered");
    expect_true(
        "typed tree profile json",
        fs::is_regular_file(root / "rendered" / "etc" / "btrfs-backup" / "profiles" / "default" / "profile.json"),
        "missing rendered profile JSON"
    );
    expect_true(
        "typed tree profile mode",
        mode_of(root / "rendered" / "etc" / "btrfs-backup" / "profiles" / "default" / "profile.json") == 0600,
        "private profile should be written as 0600"
    );
    expect_true(
        "typed tree public",
        fs::is_regular_file(root / "rendered" / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / "default.json"),
        "missing rendered public profile"
    );
    expect_true(
        "typed tree public mode",
        mode_of(root / "rendered" / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / "default.json") == 0644,
        "public profile should be written as 0644"
    );
    btrfsbackup::config::json::Json public_profile = btrfsbackup::config::json::load_json_file(
        root / "rendered" / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / "default.json"
    );
    expect_true("public target label", public_profile.at("target").at("name") == "backupdisk", "missing target label");
    expect_true("public source label", public_profile.at("sources").at(0).at("name") == "Home", "missing source label");
    expect_true("public subvolume hidden", !public_profile.at("sources").at(0).contains("subvolume"), "public profile exposes subvolume path");
    expect_true("public target device hidden", !public_profile.at("target").contains("device"), "public profile exposes device");
    expect_true("public UUID hidden", !public_profile.at("target").contains("luksUuid"), "public profile exposes UUID");
    expect_true("public paths hidden", !public_profile.contains("paths"), "public profile exposes paths");
    expect_true("public hooks hidden", !public_profile.contains("hooks"), "public profile exposes hooks");
    expect_true(
        "typed tree mount dependency",
        fs::is_regular_file(root / "rendered" / "etc" / "systemd" / "system" / "btrfs-backup@default.service.d" / "target-mount.conf"),
        "missing rendered mount dependency"
    );
    expect_true(
        "typed tree native mount",
        fs::is_regular_file(
            root / "rendered" / "etc" / "systemd" / "system" /
            "mnt-btrfs\\x2dbackup-default.mount"
        ),
        "missing rendered native mount"
    );
    expect_true(
        "typed tree artifact manifest",
        mode_of(
            root / "rendered" / "etc" / "btrfs-backup" / "profiles" /
            "default" / "managed-artifacts.json"
        ) == 0600,
        "managed artifact manifest should be written as 0600"
    );
    fs::remove_all(root);
}

void test_profile_configuration_transaction_publishes_temp_artifacts() {
    const fs::path root = test_root();
    const btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(valid_profile());
    btrfsbackup::config::ProfileArtifactRenderer renderer([] {
        return btrfsbackup::config::ConfigurationGeneration{"fedcba9876543210fedcba9876543210"};
    });
    const btrfsbackup::config::RenderedProfileArtifacts rendered = renderer.render_profile_artifacts(
        profile,
        {
            .etc_root = root / "etc",
            .udev_root = root / "udev",
            .systemd_root = root / "systemd",
            .public_root = root / "public",
        }
    );
    btrfsbackup::platform::linux::config::ProfileConfigurationTransaction transaction(rendered);

    transaction.stage();
    expect_true(
        "transaction stages private profile",
        fs::is_regular_file(transaction.staged_path(btrfsbackup::config::ProfileArtifactKind::PrivateProfile)),
        "private profile was not staged"
    );
    transaction.publish_configuration();
    expect_true(
        "transaction defers public marker",
        !fs::exists(root / "public" / "default.json"),
        "public marker was published with configuration"
    );
    transaction.publish_public_marker();
    transaction.finish();
    expect_true(
        "transaction publishes public marker",
        fs::is_regular_file(root / "public" / "default.json"),
        "public marker was not published"
    );
    fs::remove_all(root);
}

void test_profile_installer() {
    fs::path root = test_root();
    btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(valid_profile());

    install_test_profile_transactionally(
        profile,
        root / "etc" / "btrfs-backup",
        root / "etc" / "udev" / "rules.d",
        root / "etc" / "systemd" / "system",
        root / "public"
    );

    expect_true("typed save profile env", !fs::exists(root / "etc" / "btrfs-backup" / "profiles.d" / "default.env"), "profile env should not be saved");
    expect_true(
        "typed save profile json",
        fs::is_regular_file(root / "etc" / "btrfs-backup" / "profiles" / "default" / "profile.json"),
        "missing saved profile JSON"
    );
    expect_true(
        "typed save public",
        fs::is_regular_file(root / "public" / "default.json"),
        "missing saved public profile"
    );
    expect_true(
        "typed save mount dependency",
        fs::is_regular_file(root / "etc" / "systemd" / "system" / "btrfs-backup@default.service.d" / "target-mount.conf"),
        "missing saved mount dependency"
    );
    expect_true(
        "typed save native mount",
        fs::is_regular_file(
            root / "etc" / "systemd" / "system" /
            "mnt-btrfs\\x2dbackup-default.mount"
        ),
        "missing saved native mount"
    );

    const fs::path profile_path = root / "etc" / "btrfs-backup" / "profiles" / "default" / "profile.json";
    const fs::path public_path = root / "public" / "default.json";
    btrfsbackup::config::json::Json saved_profile = btrfsbackup::config::json::load_json_file(profile_path);
    btrfsbackup::config::json::Json public_profile = btrfsbackup::config::json::load_json_file(public_path);
    const std::string generation = saved_profile.at("configurationGeneration").get<std::string>();
    expect_true("typed save generation length", generation.size() == 32, "invalid configuration generation");
    expect_true(
        "typed save public generation",
        public_profile.at("configurationGeneration") == generation,
        "public profile generation mismatch"
    );

    std::ifstream udev_stream(root / "etc" / "udev" / "rules.d" / "99-btrfs-backup-default.rules");
    std::string udev{std::istreambuf_iterator<char>(udev_stream), std::istreambuf_iterator<char>()};
    expect_true("typed save udev generation", udev.find(generation) != std::string::npos, "udev generation mismatch");

    std::ifstream systemd_stream(
        root / "etc" / "systemd" / "system" / "btrfs-backup@default.service.d" / "target-mount.conf"
    );
    std::string systemd{std::istreambuf_iterator<char>(systemd_stream), std::istreambuf_iterator<char>()};
    expect_true(
        "typed save systemd generation",
        systemd.find("BTRFS_BACKUP_CONFIGURATION_GENERATION=" + generation) != std::string::npos,
        "systemd generation mismatch"
    );

    const char* previous_generation_env = std::getenv("BTRFS_BACKUP_CONFIGURATION_GENERATION");
    const std::optional<std::string> previous_generation = previous_generation_env == nullptr
        ? std::nullopt
        : std::optional<std::string>(previous_generation_env);
    const char* previous_fingerprint_env = std::getenv("BTRFS_BACKUP_CONFIGURATION_FINGERPRINT");
    const std::optional<std::string> previous_fingerprint = previous_fingerprint_env == nullptr
        ? std::nullopt
        : std::optional<std::string>(previous_fingerprint_env);
    unsetenv("BTRFS_BACKUP_CONFIGURATION_GENERATION");
    unsetenv("BTRFS_BACKUP_CONFIGURATION_FINGERPRINT");
    const btrfsbackup::config::LoadedProfile loaded = btrfsbackup::platform::linux::config::FileProfileRepository(
                                                          root / "etc" / "btrfs-backup"
    )
                                                          .get(btrfsbackup::ProfileId{"default"});
    setenv("BTRFS_BACKUP_CONFIGURATION_GENERATION", generation.c_str(), 1);
    setenv("BTRFS_BACKUP_CONFIGURATION_FINGERPRINT", loaded.fingerprint.value().c_str(), 1);
    expect_true(
        "typed load matching generation",
        btrfsbackup::platform::linux::config::load_profile_by_id(root / "etc" / "btrfs-backup", "default").id == btrfsbackup::ProfileId{"default"},
        "matching generation was rejected"
    );
    setenv("BTRFS_BACKUP_CONFIGURATION_GENERATION", "00000000000000000000000000000000", 1);
    expect_validation_error(
        "typed load mismatched generation",
        [&] { (void)btrfsbackup::platform::linux::config::load_profile_by_id(root / "etc" / "btrfs-backup", "default"); },
        "generation does not match"
    );
    setenv("BTRFS_BACKUP_CONFIGURATION_GENERATION", generation.c_str(), 1);
    setenv("BTRFS_BACKUP_CONFIGURATION_FINGERPRINT", "changed-fingerprint", 1);
    try {
        (void)btrfsbackup::platform::linux::config::load_profile_by_id(root / "etc" / "btrfs-backup", "default");
        fail("typed load mismatched fingerprint", "mismatched fingerprint was accepted");
    } catch (const btrfsbackup::CodedValidationError& error) {
        expect_true(
            "typed load mismatch code",
            error.error_code == btrfsbackup::ErrorCode::ConfigurationChanged,
            "mismatched fingerprint did not report ConfigurationChanged"
        );
        expect_true(
            "typed load mismatch message",
            std::string(error.what()).find("fingerprint does not match") != std::string::npos,
            "mismatched fingerprint returned the wrong message"
        );
    }
    if (previous_generation.has_value()) {
        setenv("BTRFS_BACKUP_CONFIGURATION_GENERATION", previous_generation->c_str(), 1);
    } else {
        unsetenv("BTRFS_BACKUP_CONFIGURATION_GENERATION");
    }
    if (previous_fingerprint.has_value()) {
        setenv("BTRFS_BACKUP_CONFIGURATION_FINGERPRINT", previous_fingerprint->c_str(), 1);
    } else {
        unsetenv("BTRFS_BACKUP_CONFIGURATION_FINGERPRINT");
    }
    fs::remove_all(root);
}

void test_profile_installer_replaces_obsolete_mount_transactionally() {
    const fs::path root = test_root();
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    btrfsbackup::platform::linux::filesystem::atomic_write(
        etc_root / "btrfs-backup.conf",
        "CONFIG_VERSION=1\nTARGET_MOUNT_ROOT=/srv/backup\n",
        0644
    );

    btrfsbackup::config::json::Json raw = valid_profile();
    raw["paths"] = btrfsbackup::config::json::Json::object();
    const btrfsbackup::config::Profile profile =
        btrfsbackup::config::json::profile_from_json(raw, "/srv/backup");
    const fs::path old_unit = systemd_root / "mnt-btrfs\\x2dbackup-default.mount";
    const fs::path new_unit = systemd_root / "srv-backup-default.mount";
    const fs::path manifest =
        etc_root / "profiles" / "default" / "managed-artifacts.json";
    btrfsbackup::platform::linux::filesystem::atomic_write(old_unit, "old mount unit\n", 0644);
    btrfsbackup::platform::linux::filesystem::atomic_write(
        manifest,
        btrfsbackup::config::json::dump_json({
            {"schemaVersion", 1},
            {"profileId", "default"},
            {"mounts", btrfsbackup::config::json::Json::array({{
                           {"unit", "mnt-btrfs\\x2dbackup-default.mount"},
                           {"mountPoint", "/mnt/btrfs-backup/default"},
                       }})},
        }),
        0600
    );
    const std::string old_manifest = read_text(manifest);

    int activations = 0;
    expect_validation_error(
        "obsolete mount activation rollback",
        [&] {
            install_test_profile_transactionally(
                profile,
                etc_root,
                udev_root,
                systemd_root,
                public_root,
                [&] {
                    if (++activations == 1) {
                        throw ValidationError("injected mount activation failure");
                    }
                }
            );
        },
        "injected mount activation failure"
    );
    expect_true("obsolete mount rollback old unit", fs::is_regular_file(old_unit), "old mount was not restored");
    expect_true("obsolete mount rollback new unit", !fs::exists(new_unit), "new mount survived rollback");
    expect_true("obsolete mount rollback manifest", read_text(manifest) == old_manifest, "old manifest was not restored");

    install_test_profile_transactionally(profile, etc_root, udev_root, systemd_root, public_root);
    expect_true("obsolete mount removed", !fs::exists(old_unit), "obsolete mount unit remains");
    expect_true("replacement mount installed", fs::is_regular_file(new_unit), "replacement mount unit is missing");
    fs::remove_all(root);
}

void test_profile_installation_staging_failure_preserves_installed_artifacts() {
    fs::path root = test_root();
    btrfsbackup::config::Profile original = btrfsbackup::config::json::profile_from_json(valid_profile());
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    install_test_profile_transactionally(original, etc_root, udev_root, systemd_root, public_root);

    const fs::path profile_path = etc_root / "profiles" / "default" / "profile.json";
    const fs::path udev_path = udev_root / "99-btrfs-backup-default.rules";
    const fs::path systemd_path = systemd_root / "btrfs-backup@default.service.d" / "target-mount.conf";
    const fs::path public_path = public_root / "default.json";
    const std::array<std::string, 4> before{
        read_text(profile_path),
        read_text(udev_path),
        read_text(systemd_path),
        read_text(public_path),
    };
    btrfsbackup::config::Profile changed = original;
    changed.name = "Changed profile";
    const fs::path blocked_public_root = root / "blocked-public";
    btrfsbackup::platform::linux::filesystem::atomic_write(blocked_public_root, "not a directory", 0600);

    bool rejected = false;
    try {
        install_test_profile_transactionally(changed, etc_root, udev_root, systemd_root, blocked_public_root);
    } catch (const btrfsbackup::platform::linux::config::ConfigurationSaveError& error) {
        rejected = true;
        expect_true(
            "transaction staging failure code",
            error.error_code == btrfsbackup::ErrorCode::ConfigurationSaveFailed,
            "unexpected error code: " + btrfsbackup::error_code_name(error.error_code)
        );
        expect_true(
            "transaction staging rollback complete",
            error.rollback_result.complete && error.rollback_result.errors.empty(),
            "staging failure unexpectedly required rollback"
        );
    } catch (const std::exception& error) {
        fail("transaction staging failure", std::string("unexpected exception: ") + error.what());
    }
    expect_true("transaction staging failure", rejected, "save unexpectedly succeeded");
    expect_true(
        "transaction preserves profile",
        read_text(profile_path) == before[0],
        "profile changed after staging failure"
    );
    expect_true("transaction preserves udev", read_text(udev_path) == before[1], "udev changed after staging failure");
    expect_true("transaction preserves systemd", read_text(systemd_path) == before[2], "systemd changed after staging failure");
    expect_true("transaction preserves public", read_text(public_path) == before[3], "public profile changed after staging failure");
    fs::remove_all(root);
}

void test_profile_installer_create_only_refuses_existing_profile() {
    const fs::path root = test_root();
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    btrfsbackup::config::Profile original = btrfsbackup::config::json::profile_from_json(valid_profile());
    install_test_profile_transactionally(original, etc_root, udev_root, systemd_root, public_root);
    const fs::path profile_path = etc_root / "profiles" / "default" / "profile.json";
    const std::string before = read_text(profile_path);

    btrfsbackup::config::Profile replacement = original;
    replacement.name = "Replacement that must not be published";
    btrfsbackup::config::ProfileArtifactRenderer renderer(
        btrfsbackup::platform::linux::config::generate_configuration_generation
    );
    btrfsbackup::config::NullConfigurationActivator activator;
    btrfsbackup::platform::linux::config::ProfileInstaller installer(renderer, activator);
    const btrfsbackup::platform::linux::config::ExpectedProfileIdentity create_only{
        .exists = false,
        .generation = {},
        .fingerprint = {},
    };
    bool rejected = false;
    try {
        installer.install_profile_transactionally(
            replacement,
            {.etc_root = etc_root, .udev_root = udev_root, .systemd_root = systemd_root, .public_root = public_root},
            &create_only
        );
    } catch (const btrfsbackup::CodedValidationError& error) {
        rejected = error.error_code == btrfsbackup::ErrorCode::ConfigurationChanged;
    }
    expect_true("create-only profile publication", rejected, "existing profile was replaced");
    expect_true(
        "create-only preserves existing profile",
        read_text(profile_path) == before,
        "create-only failure modified the installed profile"
    );
    fs::remove_all(root);
}

void test_profile_installation_activation_failure_rolls_back_all_artifacts() {
    fs::path root = test_root();
    btrfsbackup::config::Profile original = btrfsbackup::config::json::profile_from_json(valid_profile());
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    const fs::path profile_path = etc_root / "profiles" / "default" / "profile.json";
    const fs::path udev_path = udev_root / "99-btrfs-backup-default.rules";
    const fs::path systemd_path = systemd_root / "btrfs-backup@default.service.d" / "target-mount.conf";
    const fs::path public_path = public_root / "default.json";
    install_test_profile_transactionally(original, etc_root, udev_root, systemd_root, public_root);

    const std::array<std::string, 4> before{
        read_text(profile_path),
        read_text(udev_path),
        read_text(systemd_path),
        read_text(public_path),
    };
    btrfsbackup::config::Profile changed = original;
    changed.name = "Changed profile";
    int activation_calls = 0;
    bool public_marker_was_old_during_activation = false;
    expect_validation_error(
        "transaction activation failure",
        [&] {
            install_test_profile_transactionally(
                changed,
                etc_root,
                udev_root,
                systemd_root,
                public_root,
                [&] {
                    ++activation_calls;
                    public_marker_was_old_during_activation = read_text(public_path) == before[3];
                    if (activation_calls == 1) {
                        throw ValidationError("injected activation failure");
                    }
                }
            );
        },
        "injected activation failure"
    );

    expect_true("transaction reloads rollback", activation_calls == 2, "old configuration was not reactivated");
    expect_true(
        "transaction public marker commits last",
        public_marker_was_old_during_activation,
        "public profile was published before activation"
    );
    expect_true("transaction restores private profile", read_text(profile_path) == before[0], "private profile changed");
    expect_true("transaction restores udev rule", read_text(udev_path) == before[1], "udev rule changed");
    expect_true("transaction restores systemd drop-in", read_text(systemd_path) == before[2], "systemd drop-in changed");
    expect_true("transaction preserves public marker", read_text(public_path) == before[3], "public profile changed");
    fs::remove_all(root);
}

void test_profile_installation_reports_incomplete_rollback() {
    fs::path root = test_root();
    btrfsbackup::config::Profile original = btrfsbackup::config::json::profile_from_json(valid_profile());
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    const fs::path udev_path = udev_root / "99-btrfs-backup-default.rules";
    install_test_profile_transactionally(original, etc_root, udev_root, systemd_root, public_root);

    btrfsbackup::config::Profile changed = original;
    changed.name = "Changed profile";
    int activation_calls = 0;
    try {
        install_test_profile_transactionally(
            changed,
            etc_root,
            udev_root,
            systemd_root,
            public_root,
            [&] {
                ++activation_calls;
                if (activation_calls != 1) {
                    return;
                }
                fs::remove(udev_path);
                fs::create_directory(udev_path);
                btrfsbackup::platform::linux::filesystem::atomic_write(udev_path / "blocker", "injected rollback failure", 0600);
                throw ValidationError("injected save failure");
            }
        );
        fail("transaction incomplete rollback", "save unexpectedly succeeded");
    } catch (const btrfsbackup::platform::linux::config::ConfigurationSaveError& error) {
        expect_true(
            "transaction rollback error code",
            error.error_code == btrfsbackup::ErrorCode::ConfigurationRollbackIncomplete,
            "unexpected error code: " + btrfsbackup::error_code_name(error.error_code)
        );
        expect_true(
            "transaction rollback retains primary error",
            std::string(error.what()).find("configuration.save_failed: injected save failure") != std::string::npos,
            "primary save failure was lost"
        );
        expect_true(
            "transaction rollback reports secondary error",
            std::string(error.what()).find("configuration.rollback_incomplete") != std::string::npos,
            "rollback failure was not reported"
        );
        expect_true(
            "transaction rollback result incomplete",
            !error.rollback_result.complete && !error.rollback_result.errors.empty(),
            "rollback result contains no diagnostics"
        );
        bool reported_restore_failure = false;
        for (const btrfsbackup::platform::linux::config::RollbackError& rollback_error : error.rollback_result.errors) {
            if (rollback_error.operation == "restore previous artifact" && rollback_error.path.filename().string().starts_with(".99-btrfs-backup-default.rules.previous-")) {
                reported_restore_failure = true;
            }
        }
        expect_true(
            "transaction rollback identifies failed artifact",
            reported_restore_failure,
            "rollback diagnostics do not identify the unrestored udev rule"
        );
    } catch (const std::exception& error) {
        fail("transaction incomplete rollback", std::string("unexpected exception: ") + error.what());
    }

    bool previous_preserved = false;
    std::error_code scan_error;
    for (const fs::directory_entry& entry : fs::directory_iterator(udev_root, scan_error)) {
        const std::string filename = entry.path().filename().string();
        if (filename.starts_with(".99-btrfs-backup-default.rules.previous-")) {
            previous_preserved = fs::is_regular_file(entry.path());
        }
    }
    expect_true(
        "transaction preserves failed rollback artifact",
        previous_preserved,
        "recoverable previous artifact was removed"
    );
    fs::remove_all(root);
}

void test_profile_installation_refuses_active_profile_lock() {
    fs::path root = test_root();
    btrfsbackup::config::Profile original = btrfsbackup::config::json::profile_from_json(valid_profile());
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    const fs::path profile_path = etc_root / "profiles" / "default" / "profile.json";
    install_test_profile_transactionally(original, etc_root, udev_root, systemd_root, public_root);
    const std::string before = read_text(profile_path);

    btrfsbackup::platform::linux::filesystem::FileLock lock(
        btrfsbackup::platform::linux::filesystem::profile_lock_path(etc_root / ".locks", btrfsbackup::ProfileId{"default"})
    );
    expect_true("transaction test lock acquired", lock.try_acquire(), "cannot acquire test profile lock");
    btrfsbackup::config::Profile changed = original;
    changed.name = "Changed profile";
    expect_validation_error(
        "transaction active profile lock",
        [&] { install_test_profile_transactionally(changed, etc_root, udev_root, systemd_root, public_root); },
        "profile is active"
    );
    expect_true("transaction lock preserves profile", read_text(profile_path) == before, "profile changed while active");
    fs::remove_all(root);
}

void test_render_udev_optional_matches() {
    btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(valid_profile());
    std::string rendered = btrfsbackup::config::render_udev(profile);
    expect_true("udev partition", rendered.find("ENV{ID_PART_ENTRY_UUID}==\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\"") != std::string::npos, "missing partition UUID match");
    expect_true("udev serial", rendered.find("ENV{ID_SERIAL_SHORT}==\"SERIAL_123\"") != std::string::npos, "missing serial match");
    expect_true("udev visible name", rendered.find("ENV{UDISKS_NAME}=\"") != std::string::npos, "missing UDisks display name");
    expect_true("udev visible device", rendered.find("ENV{UDISKS_IGNORE}=\"0\"") != std::string::npos, "managed target is hidden from UDisks");

    profile.enabled = false;
    rendered = btrfsbackup::config::render_udev(profile);
    expect_true("disabled udev presentation", rendered.find("ENV{UDISKS_NAME}=\"") != std::string::npos, "disabled profile lost UDisks presentation");
    expect_true("disabled no activation", rendered.find("SYSTEMD_WANTS") == std::string::npos, "disabled profile requests automatic activation");
}

} // namespace

int main() {
    test_profile_repository_loads_profile_and_fingerprint_from_one_read();
    test_profile_repository_rejects_missing_generation();
    test_rejects_bad_uuid();
    test_rejects_bad_configuration_generation();
    test_rejects_missing_btrfs_uuid();
    test_rejects_non_dev_target();
    test_rejects_nested_roots();
    test_target_activation_is_structured();
    test_profile_round_trips_normalized_json();
    test_invalid_profile_document_does_not_create_profile();
    test_profile_rejects_old_schema_versions();
    test_profile_rejects_removed_sources_directory();
    test_profile_rejects_system_path_overrides();
    test_mount_point_is_application_controlled();
    test_profile_rejects_removed_notifications();
    test_profile_hooks_round_trip_as_explicit_program_arguments();
    test_profile_rejects_unsafe_hook_shape();
    test_profile_artifact_renderer();
    test_profile_configuration_transaction_publishes_temp_artifacts();
    test_profile_installer();
    test_profile_installer_replaces_obsolete_mount_transactionally();
    test_profile_installation_staging_failure_preserves_installed_artifacts();
    test_profile_installer_create_only_refuses_existing_profile();
    test_profile_installation_activation_failure_rolls_back_all_artifacts();
    test_profile_installation_reports_incomplete_rollback();
    test_profile_installation_refuses_active_profile_lock();
    test_render_udev_optional_matches();

    if (failures > 0) {
        return 1;
    }
    std::cout << "ok - profile module tests\n";
    return 0;
}
