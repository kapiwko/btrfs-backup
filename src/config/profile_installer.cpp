// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/profile_installer.hpp>

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <config/application_config.hpp>
#include <config/application_paths.hpp>
#include <config/errors.hpp>
#include <config/json_io.hpp>
#include <config/profile_configuration_transaction.hpp>
#include <platform/linux/file_lock.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

fs::path configuration_lock_path(const fs::path& etc_root, const std::string& profile_id) {
    if (fs::absolute(etc_root).lexically_normal() == fs::path("/etc/btrfs-backup")) {
        return profile_lock_path(default_lock_root(), profile_id);
    }
    return profile_lock_path(etc_root / ".locks", profile_id);
}

void rename_checked(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::rename(from, to, error);
    if (error) {
        throw ValidationError("cannot rename " + from.string() + " to " + to.string() + ": " + error.message());
    }
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
    std::string_view message,
    const fs::path* destination = nullptr
) noexcept {
    result.complete = false;
    try {
        std::string detail{message};
        if (destination != nullptr) {
            detail += "; destination: " + destination->string();
        }
        result.errors.push_back({std::string(operation), path, std::move(detail)});
    } catch (...) {
    }
}

} // namespace

void NullProfileActivation::activate() {
}

FunctionProfileActivation::FunctionProfileActivation(std::function<void()> activate)
    : activate_(std::move(activate)) {
    if (!activate_) {
        throw ValidationError("profile activation function is required");
    }
}

void FunctionProfileActivation::activate() {
    activate_();
}

ProfileInstaller::ProfileInstaller(ProfileArtifactRenderer& renderer, IProfileActivation& activation)
    : renderer_(renderer), activation_(activation) {
}

void ProfileInstaller::install_profile_transactionally(const Profile& profile, const ProfileArtifactRoots& roots) {
    const RenderedProfileArtifacts rendered = renderer_.render_profile_artifacts(profile, roots);
    const std::string installed_id{rendered.profile.id.value()};
    const std::string& generation = rendered.profile.configuration_generation;
    ApplicationConfig application_config = ApplicationConfig::load(roots.etc_root);
    const fs::path source_root = profile_sources_dir(application_config.paths(), installed_id);
    const fs::path source_backup = source_root.parent_path()
        / (source_root.filename().string() + ".backup-" + generation);
    ProfileConfigurationTransaction transaction(rendered);

    try {
        transaction.stage();

        const Profile staged_profile = profile_from_json(
            load_json_file(transaction.staged_path(ProfileArtifactKind::PrivateProfile)),
            application_config.paths().target_mount_root
        );
        const Json staged_public = load_json_file(transaction.staged_path(ProfileArtifactKind::PublicProfile));
        if (staged_profile.configuration_generation != generation
            || staged_public.value("configurationGeneration", "") != generation) {
            throw ValidationError("staged configuration generation mismatch");
        }

        FileLock lock(configuration_lock_path(roots.etc_root, installed_id));
        if (!lock.try_acquire()) {
            throw ValidationError("profile is active; configuration save refused: " + installed_id);
        }

        bool source_backed_up = false;
        bool activation_attempted = false;
        try {
            std::error_code source_error;
            if (fs::exists(source_root, source_error) && !source_error) {
                rename_checked(source_root, source_backup);
                source_backed_up = true;
            } else if (source_error) {
                throw ValidationError("cannot inspect legacy source configuration: " + source_error.message());
            }

            transaction.publish_configuration();
            activation_attempted = true;
            activation_.activate();
            transaction.publish_public_marker();
        } catch (...) {
            const std::string cause = current_exception_message();
            RollbackResult rollback = transaction.rollback();
            if (source_backed_up) {
                std::error_code restore_error;
                fs::rename(source_backup, source_root, restore_error);
                if (restore_error) {
                    record_rollback_error(
                        rollback,
                        "restore legacy source configuration",
                        source_backup,
                        restore_error.message(),
                        &source_root
                    );
                }
            }
            if (activation_attempted) {
                try {
                    activation_.activate();
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

} // namespace btrfsbackup
