// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/ProfileInstaller.hpp>

#include <exception>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <platform/linux/config/ApplicationConfig.hpp>
#include <core/Errors.hpp>
#include <config/json/JsonIo.hpp>
#include <config/json/ProfileDocument.hpp>
#include <config/ProfileRender.hpp>
#include <platform/linux/config/ProfileConfigurationTransaction.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/filesystem/FileLock.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux::config {

namespace {

fs::path configuration_lock_path(const fs::path& etc_root, const ProfileId& profile_id) {
    if (fs::absolute(etc_root).lexically_normal() == fs::path("/etc/btrfs-backup")) {
        return filesystem::profile_lock_path(filesystem::default_lock_root(), profile_id);
    }
    return filesystem::profile_lock_path(etc_root / ".locks", profile_id);
}

std::string current_exception_message() noexcept {
    try {
        throw;
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "unknown configuration save failure";
    }
}

void record_rollback_error(
    RollbackResult& result,
    std::string_view operation,
    const fs::path& path,
    std::string_view message
) noexcept {
    result.complete = false;
    try {
        result.errors.push_back({std::string(operation), path, std::string(message)});
    } catch (...) {
    }
}

void append_obsolete_systemd_units(
    btrfsbackup::config::RenderedProfileArtifacts& rendered,
    const btrfsbackup::config::ProfileArtifactRoots& roots
) {
    const std::string profile_id{rendered.profile.id.value()};
    const fs::path manifest_path = roots.etc_root / "profiles" / profile_id / "managed-artifacts.json";
    std::error_code error;
    const fs::file_status status = fs::symlink_status(manifest_path, error);
    if (error || status.type() == fs::file_type::not_found) {
        return;
    }
    if (fs::is_symlink(status) || !fs::is_regular_file(status)) {
        throw ValidationError("managed artifact manifest is not a regular file: " + manifest_path.string());
    }

    const btrfsbackup::config::json::Json manifest = btrfsbackup::config::json::load_json_file(manifest_path);
    if (!manifest.is_object() || manifest.size() != 3 || manifest.value("schemaVersion", 0) != 1 ||
        manifest.value("profileId", "") != profile_id || !manifest.contains("mounts") ||
        !manifest.at("mounts").is_array()) {
        throw ValidationError("invalid managed artifact manifest: " + manifest_path.string());
    }

    std::set<std::string> current_units;
    for (const btrfsbackup::config::ProfileArtifact& artifact : rendered.artifacts) {
        if (artifact.kind == btrfsbackup::config::ProfileArtifactKind::NativeTargetMount) {
            current_units.insert(artifact.destination.filename().string());
        }
    }
    for (const btrfsbackup::config::json::Json& value : manifest.at("mounts")) {
        if (!value.is_object() || value.size() != 2 || !value.contains("unit") ||
            !value.at("unit").is_string() || !value.contains("mountPoint") ||
            !value.at("mountPoint").is_string()) {
            throw ValidationError("invalid mount in managed artifact manifest");
        }
        const std::string unit = value.at("unit").get<std::string>();
        const fs::path mount_point = value.at("mountPoint").get<std::string>();
        if (!mount_point.is_absolute() || mount_point.lexically_normal() != mount_point ||
            mount_point.filename() != profile_id ||
            btrfsbackup::config::target_mount_unit_name(mount_point) != unit) {
            throw ValidationError("unsafe mount in managed artifact manifest");
        }
        if (!current_units.contains(unit)) {
            rendered.artifacts.push_back({
                .kind = btrfsbackup::config::ProfileArtifactKind::ObsoleteSystemdUnit,
                .destination = roots.systemd_root / unit,
                .content = {},
                .permissions = {},
                .operation = btrfsbackup::config::ProfileArtifactOperation::Remove,
            });
        }
    }
}

} // namespace

ProfileInstaller::ProfileInstaller(btrfsbackup::config::ProfileArtifactRenderer& renderer, btrfsbackup::config::IConfigurationActivator& activator)
    : renderer_(renderer), activator_(activator) {
}

void ProfileInstaller::install_profile_transactionally(const btrfsbackup::config::Profile& profile, const btrfsbackup::config::ProfileArtifactRoots& roots) {
    btrfsbackup::config::RenderedProfileArtifacts rendered = renderer_.render_profile_artifacts(profile, roots);
    append_obsolete_systemd_units(rendered, roots);
    const std::string installed_id{rendered.profile.id.value()};
    const btrfsbackup::config::ConfigurationGeneration& generation = rendered.profile.configuration_generation;
    btrfsbackup::config::ApplicationConfig application_config = load_application_config(roots.etc_root);
    ProfileConfigurationTransaction transaction(rendered);

    try {
        transaction.stage();

        const btrfsbackup::config::Profile staged_profile = validate_profile_file(
            transaction.staged_path(btrfsbackup::config::ProfileArtifactKind::PrivateProfile),
            application_config.paths().target_mount_root
        );
        const btrfsbackup::config::json::Json staged_public = btrfsbackup::config::json::load_json_file(transaction.staged_path(btrfsbackup::config::ProfileArtifactKind::PublicProfile));
        if (staged_profile.configuration_generation != generation || staged_public.value("configurationGeneration", "") != generation.value()) {
            throw ValidationError("staged configuration generation mismatch");
        }

        filesystem::FileLock lock(configuration_lock_path(roots.etc_root, ProfileId{installed_id}));
        if (!lock.try_acquire()) {
            throw ValidationError("profile is active; configuration save refused: " + installed_id);
        }

        bool activation_attempted = false;
        try {
            transaction.publish_configuration();
            activation_attempted = true;
            activator_.activate();
            transaction.publish_public_marker();
        } catch (...) {
            const std::string cause = current_exception_message();
            RollbackResult rollback = transaction.rollback();
            if (activation_attempted) {
                try {
                    activator_.activate();
                } catch (const std::exception& error) {
                    record_rollback_error(rollback, "reactivate previous configuration", roots.systemd_root, error.what());
                } catch (...) {
                    record_rollback_error(
                        rollback,
                        "reactivate previous configuration",
                        roots.systemd_root,
                        "unknown error"
                    );
                }
            }
            throw ConfigurationSaveError(cause, std::move(rollback));
        }
        transaction.finish();
    } catch (const ConfigurationSaveError&) {
        throw;
    } catch (...) {
        const std::string cause = current_exception_message();
        transaction.finish();
        throw ConfigurationSaveError(cause, {});
    }
}

} // namespace btrfsbackup::platform::linux::config
