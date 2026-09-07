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
#include <config/ProfileFingerprint.hpp>
#include <config/ProfileRender.hpp>
#include <platform/linux/config/ProfileConfigurationTransaction.hpp>
#include <platform/linux/config/ProfileArtifactIo.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/filesystem/TrustedFile.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux::config {

namespace {

constexpr std::size_t maximum_managed_artifact_manifest_size = 64U * 1024U;

struct ManagedArtifactManifest {
    std::optional<std::string> fingerprint;
    std::optional<btrfsbackup::config::json::Json> document;
};

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

ManagedArtifactManifest read_managed_artifact_manifest(
    const btrfsbackup::config::ProfileArtifactRoots& roots,
    const ProfileId& profile_id
) {
    const std::string id{profile_id.value()};
    const fs::path manifest_path = roots.etc_root / "profiles" / id / "managed-artifacts.json";
    std::error_code error;
    const fs::file_status status = fs::symlink_status(manifest_path, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
        return {};
    if (error)
        throw ValidationError("cannot inspect managed artifact manifest: " + manifest_path.string());

    const filesystem::TrustedFilePolicy policy{
        .allow_current_user_owner = fs::absolute(roots.etc_root).lexically_normal() != fs::path("/etc/btrfs-backup"),
    };
    const std::string bytes = filesystem::read_trusted_config_file(
        manifest_path,
        policy,
        maximum_managed_artifact_manifest_size
    );
    const btrfsbackup::config::json::Json manifest = btrfsbackup::config::json::Json::parse(bytes);
    if (!manifest.is_object() || manifest.size() != 3 || manifest.value("schemaVersion", 0) != 1 ||
        manifest.value("profileId", "") != id || !manifest.contains("mounts") ||
        !manifest.at("mounts").is_array()) {
        throw ValidationError("invalid managed artifact manifest: " + manifest_path.string());
    }
    return {
        .fingerprint = btrfsbackup::config::compute_config_fingerprint_from_bytes(
            btrfsbackup::config::current_configuration_fingerprint_version,
            manifest_path,
            bytes
        ),
        .document = manifest,
    };
}

void append_managed_systemd_units(
    std::vector<btrfsbackup::config::ProfileArtifact>& artifacts,
    const btrfsbackup::config::ProfileArtifactRoots& roots,
    const ProfileId& profile_id,
    const std::set<std::string>& retained_units,
    const ManagedArtifactManifest& manifest
) {
    if (!manifest.document.has_value())
        return;
    const std::string id{profile_id.value()};

    for (const btrfsbackup::config::json::Json& value : manifest.document->at("mounts")) {
        if (!value.is_object() || value.size() != 2 || !value.contains("unit") ||
            !value.at("unit").is_string() || !value.contains("mountPoint") ||
            !value.at("mountPoint").is_string()) {
            throw ValidationError("invalid mount in managed artifact manifest");
        }
        const std::string unit = value.at("unit").get<std::string>();
        const fs::path mount_point = value.at("mountPoint").get<std::string>();
        if (!mount_point.is_absolute() || mount_point.lexically_normal() != mount_point ||
            mount_point.filename() != id ||
            btrfsbackup::config::target_mount_unit_name(mount_point) != unit) {
            throw ValidationError("unsafe mount in managed artifact manifest");
        }
        if (!retained_units.contains(unit)) {
            artifacts.push_back({
                .kind = btrfsbackup::config::ProfileArtifactKind::ObsoleteSystemdUnit,
                .destination = roots.systemd_root / unit,
                .content = {},
                .permissions = {},
                .operation = btrfsbackup::config::ProfileArtifactOperation::Remove,
            });
        }
    }
}

void append_obsolete_systemd_units(
    btrfsbackup::config::RenderedProfileArtifacts& rendered,
    const btrfsbackup::config::ProfileArtifactRoots& roots
) {
    std::set<std::string> current_units;
    for (const btrfsbackup::config::ProfileArtifact& artifact : rendered.artifacts) {
        if (artifact.kind == btrfsbackup::config::ProfileArtifactKind::NativeTargetMount)
            current_units.insert(artifact.destination.filename().string());
    }
    append_managed_systemd_units(
        rendered.artifacts,
        roots,
        rendered.profile.id,
        current_units,
        read_managed_artifact_manifest(roots, rendered.profile.id)
    );
}

void require_expected_profile_identity(
    const btrfsbackup::config::ProfileArtifactRoots& roots,
    const ProfileId& profile_id,
    const ExpectedProfileIdentity* expected
) {
    if (expected == nullptr)
        return;
    const fs::path profile_path = roots.etc_root / "profiles" / profile_id.value() / "profile.json";
    std::error_code error;
    const fs::file_status status = fs::symlink_status(profile_path, error);
    const bool exists = !error && status.type() != fs::file_type::not_found;
    if (error && error != std::errc::no_such_file_or_directory)
        throw ValidationError("cannot inspect current profile configuration");
    if (exists != expected->exists)
        throw CodedValidationError(ErrorCode::ConfigurationChanged, "profile existence changed before commit");
    if (!exists)
        return;
    const auto loaded = FileProfileRepository(roots.etc_root).get(profile_id);
    if (loaded.generation.value() != expected->generation || loaded.fingerprint.value() != expected->fingerprint)
        throw CodedValidationError(ErrorCode::ConfigurationChanged, "profile identity changed before commit");
}

std::string unsupported_profile_fingerprint(
    const btrfsbackup::config::ProfileArtifactRoots& roots,
    const ProfileId& profile_id
) {
    const fs::path profile_path = roots.etc_root / "profiles" / profile_id.value() / "profile.json";
    const filesystem::TrustedFilePolicy policy{
        .allow_current_user_owner = fs::absolute(roots.etc_root).lexically_normal() != fs::path("/etc/btrfs-backup"),
    };
    const std::string bytes = filesystem::read_trusted_config_file(
        profile_path,
        policy,
        1024U * 1024U
    );
    return btrfsbackup::config::compute_config_fingerprint_from_bytes(
        btrfsbackup::config::current_configuration_fingerprint_version,
        profile_path,
        bytes
    );
}

std::vector<btrfsbackup::config::ProfileArtifact> unsupported_profile_artifacts(
    const btrfsbackup::config::ProfileArtifactRoots& roots,
    const ProfileId& profile_id,
    const ManagedArtifactManifest& manifest
) {
    const std::string id{profile_id.value()};
    const auto removal = [](btrfsbackup::config::ProfileArtifactKind kind, fs::path destination) {
        return btrfsbackup::config::ProfileArtifact{
            .kind = kind,
            .destination = std::move(destination),
            .content = {},
            .permissions = {},
            .operation = btrfsbackup::config::ProfileArtifactOperation::Remove,
        };
    };
    std::vector<btrfsbackup::config::ProfileArtifact> artifacts{
        removal(
            btrfsbackup::config::ProfileArtifactKind::UdevRule,
            roots.udev_root / ("99-btrfs-backup-" + id + ".rules")
        ),
        removal(
            btrfsbackup::config::ProfileArtifactKind::SystemdMountDependency,
            roots.systemd_root / ("btrfs-backup@" + id + ".service.d") / "target-mount.conf"
        ),
        removal(
            btrfsbackup::config::ProfileArtifactKind::ManagedArtifactManifest,
            roots.etc_root / "profiles" / id / "managed-artifacts.json"
        ),
        removal(
            btrfsbackup::config::ProfileArtifactKind::PrivateProfile,
            roots.etc_root / "profiles" / id / "profile.json"
        ),
        removal(
            btrfsbackup::config::ProfileArtifactKind::PublicProfile,
            roots.public_root / (id + ".json")
        ),
    };
    append_managed_systemd_units(artifacts, roots, profile_id, {}, manifest);
    return artifacts;
}

} // namespace

std::optional<std::string> read_managed_artifact_manifest_fingerprint(
    const ProfileId& profile_id,
    const btrfsbackup::config::ProfileArtifactRoots& roots
) {
    return read_managed_artifact_manifest(roots, profile_id).fingerprint;
}

ProfileInstaller::ProfileInstaller(btrfsbackup::config::ProfileArtifactRenderer& renderer, btrfsbackup::config::IConfigurationActivator& activator)
    : renderer_(renderer), activator_(activator) {
}

void ProfileInstaller::install_profile_transactionally(
    const btrfsbackup::config::Profile& profile,
    const btrfsbackup::config::ProfileArtifactRoots& roots,
    const ExpectedProfileIdentity* expected
) {
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
        require_expected_profile_identity(roots, profile.id, expected);

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
    } catch (const CodedValidationError& error) {
        transaction.finish();
        if (error.error_code == ErrorCode::ConfigurationChanged)
            throw;
        throw ConfigurationSaveError(error.what(), {});
    } catch (...) {
        const std::string cause = current_exception_message();
        transaction.finish();
        throw ConfigurationSaveError(cause, {});
    }
}

void ProfileInstaller::delete_profile_transactionally(
    const btrfsbackup::config::Profile& profile,
    const btrfsbackup::config::ProfileArtifactRoots& roots,
    const ExpectedProfileIdentity* expected
) {
    btrfsbackup::config::RenderedProfileArtifacts rendered = renderer_.render_profile_artifacts(profile, roots);
    append_obsolete_systemd_units(rendered, roots);
    for (auto& artifact : rendered.artifacts)
        artifact.operation = btrfsbackup::config::ProfileArtifactOperation::Remove;
    ProfileConfigurationTransaction transaction(rendered);
    try {
        transaction.stage();
        filesystem::FileLock lock(configuration_lock_path(roots.etc_root, profile.id));
        if (!lock.try_acquire())
            throw ValidationError("profile is active; configuration delete refused: " + std::string(profile.id.value()));
        require_expected_profile_identity(roots, profile.id, expected);
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
                    record_rollback_error(rollback, "reactivate previous configuration", roots.systemd_root, "unknown error");
                }
            }
            throw ConfigurationSaveError(cause, std::move(rollback));
        }
        transaction.finish();
    } catch (const ConfigurationSaveError&) {
        throw;
    } catch (const CodedValidationError& error) {
        transaction.finish();
        if (error.error_code == ErrorCode::ConfigurationChanged)
            throw;
        throw ConfigurationSaveError(error.what(), {});
    } catch (...) {
        const std::string cause = current_exception_message();
        transaction.finish();
        throw ConfigurationSaveError(cause, {});
    }
}

void ProfileInstaller::retire_unsupported_profile_transactionally(
    const ProfileId& profile_id,
    const std::string& expected_fingerprint,
    const std::optional<std::string>& expected_manifest_fingerprint,
    const btrfsbackup::config::ProfileArtifactRoots& roots
) {
    const ManagedArtifactManifest manifest = read_managed_artifact_manifest(roots, profile_id);
    if (manifest.fingerprint != expected_manifest_fingerprint) {
        throw CodedValidationError(
            ErrorCode::ConfigurationChanged,
            "managed artifact manifest changed before retirement"
        );
    }
    ProfileConfigurationTransaction transaction(
        generate_configuration_generation(),
        unsupported_profile_artifacts(roots, profile_id, manifest)
    );
    try {
        transaction.stage();
        filesystem::FileLock lock(configuration_lock_path(roots.etc_root, profile_id));
        if (!lock.try_acquire()) {
            throw ValidationError(
                "profile is active; unsupported configuration retirement refused: " +
                std::string(profile_id.value())
            );
        }
        if (unsupported_profile_fingerprint(roots, profile_id) != expected_fingerprint) {
            throw CodedValidationError(
                ErrorCode::ConfigurationChanged,
                "unsupported profile fingerprint changed before commit"
            );
        }
        if (read_managed_artifact_manifest(roots, profile_id).fingerprint != expected_manifest_fingerprint) {
            throw CodedValidationError(
                ErrorCode::ConfigurationChanged,
                "managed artifact manifest changed before retirement commit"
            );
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
                    record_rollback_error(
                        rollback,
                        "reactivate unsupported configuration",
                        roots.systemd_root,
                        error.what()
                    );
                } catch (...) {
                    record_rollback_error(
                        rollback,
                        "reactivate unsupported configuration",
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
    } catch (const CodedValidationError& error) {
        transaction.finish();
        if (error.error_code == ErrorCode::ConfigurationChanged)
            throw;
        throw ConfigurationSaveError(error.what(), {});
    } catch (...) {
        const std::string cause = current_exception_message();
        transaction.finish();
        throw ConfigurationSaveError(cause, {});
    }
}

} // namespace btrfsbackup::platform::linux::config
