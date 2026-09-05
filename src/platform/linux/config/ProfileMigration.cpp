// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/ProfileMigration.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <config/domain/Profile.hpp>
#include <config/json/Json.hpp>
#include <config/json/JsonIo.hpp>
#include <config/json/ProfileDocument.hpp>
#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <platform/linux/config/ApplicationConfig.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/config/ProfileRuntimePolicy.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/config/RenderDirectory.hpp>
#include <platform/linux/filesystem/FileIo.hpp>
#include <platform/linux/filesystem/TrustedFile.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux::config {

namespace {

struct SourceProfile {
    std::string id;
    fs::path path;
};

std::vector<SourceProfile> discover_source_profiles(const fs::path& etc_root) {
    const fs::path profile_root = etc_root / "profiles";
    std::error_code error;
    const fs::file_status root_status = fs::symlink_status(profile_root, error);
    if (error == std::errc::no_such_file_or_directory || (!error && !fs::exists(root_status))) {
        return {};
    }
    if (error) {
        throw ValidationError("cannot inspect profile directory " + profile_root.string() + ": " + error.message());
    }
    if (!fs::is_directory(root_status)) {
        throw ValidationError("profile root is not a directory: " + profile_root.string());
    }

    std::vector<SourceProfile> profiles;
    for (fs::directory_iterator it(profile_root, error), end; it != end; it.increment(error)) {
        if (error) {
            throw ValidationError("cannot enumerate profile directory " + profile_root.string() + ": " + error.message());
        }
        const fs::directory_entry& entry = *it;
        const fs::file_status entry_status = entry.symlink_status(error);
        if (error) {
            throw ValidationError("cannot inspect profile entry " + entry.path().string() + ": " + error.message());
        }
        if (!fs::is_directory(entry_status)) {
            throw ValidationError("unexpected non-directory entry in profile root: " + entry.path().string());
        }
        const std::string id = entry.path().filename().string();
        validate_profile_id(id);
        const fs::path profile_path = entry.path() / "profile.json";
        const fs::file_status profile_status = fs::symlink_status(profile_path, error);
        if (error || !fs::is_regular_file(profile_status)) {
            throw ValidationError("profile JSON is not a regular file: " + profile_path.string());
        }
        profiles.push_back({.id = id, .path = profile_path});
    }
    std::ranges::sort(profiles, {}, &SourceProfile::id);
    return profiles;
}

filesystem::TrustedFilePolicy profile_file_policy(const fs::path& etc_root) {
    return {
        .allow_current_user_owner = fs::absolute(etc_root).lexically_normal() != fs::path("/etc/btrfs-backup"),
    };
}

btrfsbackup::config::json::Json read_profile_json(
    const SourceProfile& source,
    const filesystem::TrustedFilePolicy& policy
) {
    const std::string bytes = filesystem::read_trusted_config_file(source.path, policy);
    try {
        return btrfsbackup::config::json::Json::parse(bytes);
    } catch (const btrfsbackup::config::json::Json::exception& error) {
        throw ValidationError("cannot read JSON profile " + source.path.string() + ": " + error.what());
    }
}

int schema_version(const btrfsbackup::config::json::Json& raw) {
    if (!raw.is_object() || !raw.contains("schemaVersion") || !raw.at("schemaVersion").is_number_integer()) {
        throw ValidationError("schemaVersion must be an integer");
    }
    return raw.at("schemaVersion").get<int>();
}

fs::path application_config_path(const fs::path& etc_root) {
    return fs::absolute(etc_root).lexically_normal() == fs::path("/etc/btrfs-backup")
        ? fs::path("/etc/btrfs-backup.conf")
        : etc_root / "btrfs-backup.conf";
}

std::optional<std::string> read_application_config_backup(const fs::path& etc_root) {
    const fs::path path = application_config_path(etc_root);
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory || (!error && !fs::exists(status))) {
        return std::nullopt;
    }
    if (error || !fs::is_regular_file(status)) {
        throw ValidationError("application configuration is not a regular file: " + path.string());
    }
    return filesystem::read_trusted_config_file(
        path,
        {
            .allow_current_user_owner = fs::absolute(etc_root).lexically_normal() != fs::path("/etc/btrfs-backup"),
            .allow_group_other_read = true,
        }
    );
}

} // namespace

ProfileMigrationPreflight inspect_profile_migration_readiness(const fs::path& etc_root) {
    ProfileMigrationPreflight result;
    std::optional<btrfsbackup::config::ApplicationConfig> application_config;
    try {
        application_config = load_application_config(etc_root);
    } catch (const std::exception& error) {
        result.issues.push_back({.profile_id = "<application-config>", .message = error.what()});
    }
    std::vector<SourceProfile> profiles;
    try {
        profiles = discover_source_profiles(etc_root);
    } catch (const std::exception& error) {
        result.issues.push_back({.profile_id = "<profile-root>", .message = error.what()});
        return result;
    }

    const filesystem::TrustedFilePolicy policy = profile_file_policy(etc_root);
    for (const SourceProfile& source : profiles) {
        try {
            const auto raw = read_profile_json(source, policy);
            const int version = schema_version(raw);
            if (version != btrfsbackup::config::json::current_profile_schema_version) {
                throw ValidationError(
                    "schema version " + std::to_string(version) +
                    " must be exported to schema version 4 before upgrading"
                );
            }
            if (!application_config.has_value()) {
                continue;
            }
            const btrfsbackup::config::LoadedProfile loaded =
                FileProfileRepository(etc_root, *application_config).get(ProfileId{source.id});
            if (loaded.profile.id.value() != source.id) {
                throw ValidationError("profileId does not match its configuration directory");
            }
            result.ready_profiles.push_back(source.id);
        } catch (const std::exception& error) {
            result.issues.push_back({.profile_id = source.id, .message = error.what()});
        }
    }
    return result;
}

std::vector<std::string> export_all_profiles_v4(const fs::path& etc_root, const fs::path& output_dir) {
    std::error_code output_error;
    const fs::file_status output_status = fs::symlink_status(output_dir, output_error);
    if ((!output_error && fs::exists(output_status)) ||
        (output_error && output_error != std::errc::no_such_file_or_directory)) {
        throw ValidationError("refusing to overwrite profile migration backup: " + output_dir.string());
    }
    const std::vector<SourceProfile> sources = discover_source_profiles(etc_root);
    const filesystem::TrustedFilePolicy policy = profile_file_policy(etc_root);
    const btrfsbackup::config::ApplicationConfig application_config = load_application_config(etc_root);
    const std::optional<std::string> application_config_backup = read_application_config_backup(etc_root);
    std::vector<btrfsbackup::config::Profile> profiles;
    profiles.reserve(sources.size());

    for (const SourceProfile& source : sources) {
        const auto raw = read_profile_json(source, policy);
        const auto normalized = btrfsbackup::config::json::normalize_profile_for_v4_export(
            raw,
            application_config.paths().target_mount_root
        );
        btrfsbackup::config::Profile profile = btrfsbackup::config::json::profile_from_json(
            normalized,
            application_config.paths().target_mount_root
        );
        validate_profile_runtime_policy(profile);
        if (profile.id.value() != source.id) {
            throw ValidationError("profileId does not match its configuration directory: " + source.id);
        }
        profiles.push_back(std::move(profile));
    }

    replace_render_directory(
        output_dir,
        [&](const fs::path& staging) {
            for (const auto& profile : profiles) {
                write_profile_file(profile, staging / "profiles" / profile.id.value() / "profile.json");
            }
            if (application_config_backup.has_value()) {
                filesystem::atomic_write(staging / "btrfs-backup.conf", *application_config_backup, 0600);
            }
            filesystem::atomic_write(
                staging / "RESTORE.txt",
                "btrfs-backup schema-v4 profile export\n\n"
                "After installing 4.0, restore each profile with:\n"
                "  sudo btrfs-backupctl profile save --file profiles/PROFILE/profile.json\n\n"
                "If btrfs-backup.conf is present, review and restore it before saving profiles.\n"
                "Referenced key files are intentionally not copied. Back them up separately.\n",
                0600
            );
        },
        [&](const fs::path& staging) {
            for (const auto& profile : profiles) {
                const auto validated = validate_profile_file(
                    staging / "profiles" / profile.id.value() / "profile.json",
                    application_config.paths().target_mount_root
                );
                if (validated.id != profile.id) {
                    throw ValidationError("exported profile identity mismatch");
                }
            }
        }
    );

    std::vector<std::string> ids;
    ids.reserve(profiles.size());
    for (const auto& profile : profiles) {
        ids.emplace_back(profile.id.value());
    }
    return ids;
}

} // namespace btrfsbackup::platform::linux::config
