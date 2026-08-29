// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/profile_repository.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

#include <core/errors.hpp>
#include <platform/linux/config/application_config.hpp>
#include <platform/linux/config/profile_legacy_runtime_policy.hpp>
#include <platform/linux/config/profile_runtime_policy.hpp>
#include <config/profile_fingerprint.hpp>
#include <core/identifiers.hpp>
#include <config/model/json.hpp>
#include <config/model/profile.hpp>
#include <config/model/profile_document.hpp>
#include <platform/linux/trusted_file.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

fs::path profile_json_path(const fs::path& etc_root, const std::string& profile_id) {
    validate_identifier(profile_id, "profile");
    return etc_root / "profiles" / profile_id / "profile.json";
}

namespace {

ProfileFileReader trusted_profile_reader(const fs::path& config_root) {
    TrustedFilePolicy policy{
        .allow_current_user_owner = fs::absolute(config_root).lexically_normal() != fs::path("/etc/btrfs-backup"),
    };
    return [policy](const fs::path& path) { return read_trusted_config_file(path, policy); };
}

btrfsbackup::config::LoadedProfile loaded_profile_from_bytes(
    const std::string& bytes,
    const fs::path& path,
    const btrfsbackup::config::ApplicationPaths& application_paths
) {
    try {
        const btrfsbackup::config::Json raw = btrfsbackup::config::Json::parse(bytes);
        validate_legacy_profile_runtime_fields(raw, application_paths.target_mount_root);
        const btrfsbackup::config::ProfileDocument document = btrfsbackup::config::normalize_profile_document(
            raw,
            application_paths.target_mount_root
        );
        btrfsbackup::config::Profile profile = btrfsbackup::config::profile_from_document(
            document,
            application_paths.target_mount_root
        );
        validate_profile_runtime_policy(profile);
        const btrfsbackup::config::ConfigurationGeneration generation = profile.configuration_generation;
        return {
            .profile = std::move(profile),
            .fingerprint = btrfsbackup::config::ConfigurationFingerprint(
                btrfsbackup::config::compute_config_fingerprint_from_bytes(
                    btrfsbackup::config::current_configuration_fingerprint_version,
                    path,
                    bytes
                )
            ),
            .generation = generation,
        };
    } catch (const btrfsbackup::config::Json::exception& exc) {
        throw ValidationError("cannot read JSON profile " + path.string() + ": " + exc.what());
    }
}

void validate_expected_identity(const btrfsbackup::config::LoadedProfile& loaded) {
    const char* expected_generation = std::getenv(
        btrfsbackup::config::expected_configuration_generation_environment
    );
    if (expected_generation != nullptr && loaded.generation.value() != expected_generation) {
        throw CodedValidationError(
            ErrorCode::ConfigurationChanged,
            "profile configuration generation does not match the authorized operation"
        );
    }
    const char* expected_fingerprint = std::getenv(
        btrfsbackup::config::expected_configuration_fingerprint_environment
    );
    if (expected_fingerprint != nullptr && loaded.fingerprint.value() != expected_fingerprint) {
        throw CodedValidationError(
            ErrorCode::ConfigurationChanged,
            "profile configuration fingerprint does not match the authorized operation"
        );
    }
}

} // namespace

btrfsbackup::config::Profile load_profile_by_id(const fs::path& etc_root, const std::string& profile_id) {
    return FileProfileRepository(etc_root).get(ProfileId{profile_id}).profile;
}

FileProfileRepository::FileProfileRepository(fs::path config_root)
    : FileProfileRepository(config_root, load_application_config(config_root), trusted_profile_reader(config_root)) {
}

FileProfileRepository::FileProfileRepository(fs::path config_root, btrfsbackup::config::ApplicationConfig application_config)
    : FileProfileRepository(config_root, std::move(application_config), trusted_profile_reader(config_root)) {
}

FileProfileRepository::FileProfileRepository(
    fs::path config_root,
    btrfsbackup::config::ApplicationConfig application_config,
    ProfileFileReader profile_reader
)
    : config_root_(std::move(config_root)),
      application_config_(std::move(application_config)),
      profile_reader_(std::move(profile_reader)) {
}

btrfsbackup::config::LoadedProfile FileProfileRepository::get(const ProfileId& profile_id) const {
    const fs::path path = profile_json_path(config_root_, std::string(profile_id.value()));
    btrfsbackup::config::LoadedProfile loaded = loaded_profile_from_bytes(
        profile_reader_(path),
        path,
        application_config_.paths()
    );
    validate_expected_identity(loaded);
    return loaded;
}

} // namespace btrfsbackup::platform::linux
