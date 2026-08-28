// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/profile_repository.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

#include <core/errors.hpp>
#include <platform/linux/config/application_config.hpp>
#include <config/profile_fingerprint.hpp>
#include <core/identifiers.hpp>
#include <config/model/json.hpp>
#include <config/model/profile.hpp>
#include <platform/linux/trusted_file.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

fs::path profile_json_path(const fs::path& etc_root, const std::string& profile_id) {
    validate_identifier(profile_id, "profile");
    return etc_root / "profiles" / profile_id / "profile.json";
}

btrfsbackup::config::Json load_profile_json_by_id(const fs::path& etc_root, const std::string& profile_id) {
    fs::path canonical = profile_json_path(etc_root, profile_id);
    TrustedFilePolicy policy{
        .allow_current_user_owner = fs::absolute(etc_root).lexically_normal() != fs::path("/etc/btrfs-backup"),
    };
    try {
        btrfsbackup::config::ApplicationConfig config = load_application_config(etc_root);
        return btrfsbackup::config::normalize_profile(btrfsbackup::config::Json::parse(read_trusted_config_file(canonical, policy)), config.paths().target_mount_root);
    } catch (const btrfsbackup::config::Json::exception& exc) {
        throw ValidationError("cannot read JSON profile " + canonical.string() + ": " + exc.what());
    }
}

btrfsbackup::config::Profile load_profile_by_id(const fs::path& etc_root, const std::string& profile_id) {
    btrfsbackup::config::ApplicationConfig config = load_application_config(etc_root);
    btrfsbackup::config::Profile profile = btrfsbackup::config::profile_from_json(load_profile_json_by_id(etc_root, profile_id), config.paths().target_mount_root);
    const char* expected_generation = std::getenv("BTRFS_BACKUP_CONFIGURATION_GENERATION");
    if (expected_generation != nullptr && profile.configuration_generation != expected_generation) {
        throw ValidationError("profile configuration generation does not match the active systemd unit");
    }
    return profile;
}

FileProfileRepository::FileProfileRepository(fs::path config_root)
    : FileProfileRepository(config_root, load_application_config(config_root)) {
}

FileProfileRepository::FileProfileRepository(fs::path config_root, btrfsbackup::config::ApplicationConfig application_config)
    : config_root_(std::move(config_root)), application_config_(std::move(application_config)) {
}

btrfsbackup::config::Profile FileProfileRepository::get(const ProfileId& profile_id) const {
    return load_profile_by_id(config_root_, std::string(profile_id.value()));
}

const btrfsbackup::config::ApplicationPaths& FileProfileRepository::application_paths() const {
    return application_config_.paths();
}

std::string FileProfileRepository::fingerprint(const btrfsbackup::config::Profile& profile) const {
    return btrfsbackup::config::compute_config_fingerprint(
        "2.0.0",
        config_root_ / "profiles" / profile.id.value() / "profile.json",
        {}
    );
}

} // namespace btrfsbackup::platform::linux
