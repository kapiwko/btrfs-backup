// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/profile_installer.hpp>

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <platform/linux/config/application_config.hpp>
#include <core/errors.hpp>
#include <config/model/json_io.hpp>
#include <config/model/profile_document.hpp>
#include <platform/linux/config/profile_configuration_transaction.hpp>
#include <platform/linux/file_lock.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

namespace {

fs::path configuration_lock_path(const fs::path& etc_root, const std::string& profile_id) {
    if (fs::absolute(etc_root).lexically_normal() == fs::path("/etc/btrfs-backup")) {
        return profile_lock_path(default_lock_root(), profile_id);
    }
    return profile_lock_path(etc_root / ".locks", profile_id);
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

} // namespace

ProfileInstaller::ProfileInstaller(btrfsbackup::config::ProfileArtifactRenderer& renderer, btrfsbackup::config::IConfigurationActivator& activator)
    : renderer_(renderer), activator_(activator) {
}

void ProfileInstaller::install_profile_transactionally(const btrfsbackup::config::Profile& profile, const btrfsbackup::config::ProfileArtifactRoots& roots) {
    const btrfsbackup::config::RenderedProfileArtifacts rendered = renderer_.render_profile_artifacts(profile, roots);
    const std::string installed_id{rendered.profile.id.value()};
    const std::string& generation = rendered.profile.configuration_generation;
    btrfsbackup::config::ApplicationConfig application_config = load_application_config(roots.etc_root);
    ProfileConfigurationTransaction transaction(rendered);

    try {
        transaction.stage();

        const btrfsbackup::config::Profile staged_profile = btrfsbackup::config::profile_from_json(
            btrfsbackup::config::load_json_file(transaction.staged_path(btrfsbackup::config::ProfileArtifactKind::PrivateProfile)),
            application_config.paths().target_mount_root
        );
        const btrfsbackup::config::Json staged_public = btrfsbackup::config::load_json_file(transaction.staged_path(btrfsbackup::config::ProfileArtifactKind::PublicProfile));
        if (staged_profile.configuration_generation != generation || staged_public.value("configurationGeneration", "") != generation) {
            throw ValidationError("staged configuration generation mismatch");
        }

        FileLock lock(configuration_lock_path(roots.etc_root, installed_id));
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

} // namespace btrfsbackup::platform::linux
