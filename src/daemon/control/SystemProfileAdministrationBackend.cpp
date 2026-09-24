// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemProfileAdministrationBackend.hpp>
#include <daemon/control/ProfileStateQuarantine.hpp>
#include <daemon/control/ProvisioningSource.hpp>
#include <daemon/control/StoredPermissionEvaluator.hpp>

#include <array>
#include <cstdio>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include <config/json/JsonIo.hpp>
#include <config/json/ProfileDocument.hpp>
#include <config/ProfileFingerprint.hpp>
#include <config/ProfileFingerprint.hpp>
#include <core/ManagerProtocol.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <core/Errors.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>
#include <platform/linux/filesystem/TrustedFile.hpp>
#include <platform/linux/storage/MountInfo.hpp>

namespace btrfsbackup::daemon::control {

namespace {

bool hooks_equal(const config::ProfileHooks& left, const config::ProfileHooks& right) {
    const auto commands_equal = [](const auto& first, const auto& second) {
        if (first.size() != second.size())
            return false;
        for (std::size_t index = 0; index < first.size(); ++index) {
            if (first[index].program.value() != second[index].program.value() ||
                first[index].arguments != second[index].arguments || first[index].timeout != second[index].timeout)
                return false;
        }
        return true;
    };
    return commands_equal(left.before_snapshot, right.before_snapshot) &&
        commands_equal(left.after_snapshot, right.after_snapshot);
}

bool hooks_empty(const config::ProfileHooks& hooks) {
    return hooks.before_snapshot.empty() && hooks.after_snapshot.empty();
}

bool has_mount_option(std::string_view options, std::string_view expected) {
    while (!options.empty()) {
        const auto separator = options.find(',');
        if (options.substr(0, separator) == expected)
            return true;
        if (separator == std::string_view::npos)
            break;
        options.remove_prefix(separator + 1U);
    }
    return false;
}

std::string source_candidate_id(const SourceCandidate& candidate) {
    std::string identity;
    identity.reserve(
        candidate.filesystem_uuid.size() + candidate.path.size() + candidate.mount_root.size() +
        candidate.local_snapshot_root.size() + 4U
    );
    for (const std::string* value : {
             &candidate.filesystem_uuid,
             &candidate.path,
             &candidate.mount_root,
             &candidate.local_snapshot_root,
         }) {
        identity.append(*value);
        identity.push_back('\0');
    }
    return config::compute_config_fingerprint_from_bytes(
        "profile-source-candidate-v1",
        "candidate",
        identity
    );
}

std::filesystem::path path_from_descriptor(int descriptor) {
    if (descriptor < 0)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "source descriptor is invalid");
    const int flags = ::fcntl(descriptor, F_GETFL);
    if (flags < 0 || (flags & O_PATH) != O_PATH)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "source descriptor must use O_PATH");
    struct stat descriptor_status{};
    if (::fstat(descriptor, &descriptor_status) != 0 || !S_ISDIR(descriptor_status.st_mode))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "source descriptor must identify a directory");
    const std::string proc_path = "/proc/self/fd/" + std::to_string(descriptor);
    std::array<char, 4096> resolved{};
    const ssize_t length = ::readlink(proc_path.c_str(), resolved.data(), resolved.size());
    if (length < 0 || static_cast<std::size_t>(length) == resolved.size())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "cannot resolve source descriptor");
    const std::filesystem::path path{std::string(resolved.data(), static_cast<std::size_t>(length))};
    if (!path.is_absolute() || path.string().ends_with(" (deleted)"))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "source descriptor no longer identifies a local path");
    struct stat path_status{};
    if (::stat(path.c_str(), &path_status) != 0 || path_status.st_dev != descriptor_status.st_dev ||
        path_status.st_ino != descriptor_status.st_ino) {
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "source path changed after it was selected");
    }
    return path.lexically_normal();
}

platform::linux::OwnedFileDescriptor open_for_caller_access(
    const std::filesystem::path& path,
    const BrowseAccessIdentity& identity
) {
    platform::linux::OwnedFileDescriptor current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!current.valid())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceUnavailable, "cannot inspect source directory permissions");
    StoredPermissionEvaluator::require_access(current.get(), &identity, 1, "source directory permissions deny access");
    for (const auto& component : path.relative_path()) {
        if (component.empty() || component == ".")
            continue;
        if (component == "..")
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "source path contains parent traversal");
        platform::linux::OwnedFileDescriptor next(
            ::openat(current.get(), component.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
        );
        if (!next.valid())
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceUnavailable, "cannot inspect source directory permissions");
        StoredPermissionEvaluator::require_access(next.get(), &identity, 1, "source directory permissions deny access");
        current = std::move(next);
    }
    return current;
}

} // namespace

SystemProfileAdministrationBackend::SystemProfileAdministrationBackend(
    ProfileAdministrationRoots roots,
    std::filesystem::path target_mount_root,
    std::filesystem::path mountinfo_path,
    backup::IBtrfsOperations& btrfs,
    config::IConfigurationActivator& activator
) : roots_(std::move(roots)), target_mount_root_(std::move(target_mount_root)),
    mountinfo_path_(std::move(mountinfo_path)), btrfs_(btrfs), activator_(activator) {
}

SourceSubvolumeState SystemProfileAdministrationBackend::inspect_source_subvolume(const std::filesystem::path& path) const {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error)
        return SourceSubvolumeState::Unavailable;
    if (!exists)
        return SourceSubvolumeState::Missing;
    try {
        return btrfs_.is_subvolume(path) ? SourceSubvolumeState::Available : SourceSubvolumeState::NotSubvolume;
    } catch (...) {
        return SourceSubvolumeState::Unavailable;
    }
}

std::vector<ProfileSourceCandidate> SystemProfileAdministrationBackend::source_candidates() const {
    try {
        const auto mounts = platform::linux::storage::read_mount_table(mountinfo_path_);
        std::vector<ProfileSourceCandidate> result;
        for (const SourceCandidate& candidate : provisioning_source_candidates(mounts)) {
            const auto local_mount = backup::mount_for_path(mounts, candidate.local_snapshot_root);
            if (!local_mount.has_value() || local_mount->filesystem_uuid != candidate.filesystem_uuid)
                continue;
            result.push_back({
                .id = source_candidate_id(candidate),
                .subvolume = candidate.path,
                .filesystem_uuid = candidate.filesystem_uuid,
                .mount_root = candidate.mount_root,
                .local_snapshot_root = candidate.local_snapshot_root,
                .subvolume_uuid = {},
            });
        }
        return result;
    } catch (...) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::SourceDiscoveryFailed,
            "could not discover profile source candidates"
        );
    }
}

ProfileSourceCandidate SystemProfileAdministrationBackend::resolve_source_candidate(
    const std::filesystem::path& path
) const {
    try {
        const auto mounts = platform::linux::storage::read_mount_table(mountinfo_path_);
        const auto source_mount = backup::mount_for_path(mounts, path);
        if (!source_mount.has_value() || source_mount->fstype != "btrfs" ||
            source_mount->filesystem_uuid.empty() || !has_mount_option(source_mount->options, "rw")) {
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceUnavailable, "source is not on a writable identified Btrfs filesystem");
        }
        const std::filesystem::path mount_root = std::filesystem::path(source_mount->target).lexically_normal();
        const std::filesystem::path local_snapshot_root = mount_root / ".snapshots" / "btrfs-backup";
        const auto local_mount = backup::mount_for_path(mounts, local_snapshot_root);
        if (!local_mount.has_value() || local_mount->filesystem_uuid != source_mount->filesystem_uuid)
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceUnavailable, "snapshot root is not on the source filesystem");
        const std::filesystem::path normalized = path.lexically_normal();
        const auto metadata = btrfs_.read_snapshot_metadata(normalized);
        if (!metadata.has_value() || !metadata->is_subvolume || metadata->uuid.empty())
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceNotSubvolume, "source path is not a Btrfs subvolume");
        const SourceCandidate source{
            .id = {},
            .path = normalized.string(),
            .filesystem_uuid = source_mount->filesystem_uuid,
            .mount_root = mount_root.string(),
            .local_snapshot_root = local_snapshot_root.lexically_normal().string(),
        };
        return {
            .id = source_candidate_id(source),
            .subvolume = source.path,
            .filesystem_uuid = source.filesystem_uuid,
            .mount_root = source.mount_root,
            .local_snapshot_root = source.local_snapshot_root,
            .subvolume_uuid = metadata->uuid.value(),
        };
    } catch (const dbus::ManagerOperationError&) {
        throw;
    } catch (...) {
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceDiscoveryFailed, "could not resolve selected profile source");
    }
}

ProfileSourceCandidate SystemProfileAdministrationBackend::source_candidate_from_descriptor(
    int descriptor,
    const BrowseAccessIdentity& identity
) const {
    const std::filesystem::path path = path_from_descriptor(descriptor);
    platform::linux::OwnedFileDescriptor permission_descriptor = open_for_caller_access(path, identity);
    struct stat selected_status{};
    struct stat permission_status{};
    if (::fstat(descriptor, &selected_status) != 0 ||
        ::fstat(permission_descriptor.get(), &permission_status) != 0 ||
        selected_status.st_dev != permission_status.st_dev || selected_status.st_ino != permission_status.st_ino) {
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "source descriptor identity changed");
    }
    StoredPermissionEvaluator::require_access(
        permission_descriptor.get(),
        &identity,
        1,
        "source directory permissions deny access"
    );
    return resolve_source_candidate(path);
}

std::optional<EditableProfile> SystemProfileAdministrationBackend::find_profile(const ProfileId& profile_id) const {
    std::error_code error;
    const auto path = roots_.etc_root / "profiles" / profile_id.value() / "profile.json";
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
        return std::nullopt;
    if (error)
        throw ValidationError("cannot inspect profile configuration");
    const config::LoadedProfile loaded = platform::linux::config::FileProfileRepository(roots_.etc_root).get(profile_id);
    return EditableProfile{
        .profile_id = std::string(profile_id.value()),
        .generation = loaded.generation.value(),
        .fingerprint = loaded.fingerprint.value(),
        .document = config::json::dump_json(config::json::profile_to_json(loaded.profile)),
    };
}

UnsupportedProfileIdentity SystemProfileAdministrationBackend::inspect_unsupported_profile(
    const ProfileId& profile_id
) const {
    const auto path = platform::linux::config::profile_json_path(
        roots_.etc_root,
        std::string(profile_id.value())
    );
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found) {
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile does not exist");
    }
    if (error)
        throw ValidationError("cannot inspect profile configuration");
    const platform::linux::filesystem::TrustedFilePolicy policy{
        .allow_current_user_owner = std::filesystem::absolute(roots_.etc_root).lexically_normal() !=
            std::filesystem::path("/etc/btrfs-backup"),
    };
    const std::string bytes = platform::linux::filesystem::read_trusted_config_file(
        path,
        policy,
        1024U * 1024U
    );
    int detected_schema_version = 0;
    try {
        const config::json::Json document = config::json::Json::parse(bytes);
        if (!document.is_object() || !document.contains("schemaVersion") ||
            !document.at("schemaVersion").is_number_integer()) {
            throw ValidationError("profile configuration has no integer schema version");
        }
        detected_schema_version = document.at("schemaVersion").get<int>();
    } catch (const config::json::Json::exception& error) {
        throw ValidationError("cannot inspect profile schema: " + std::string(error.what()));
    }
    if (detected_schema_version == manager_protocol::profile_schema_version) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::InvalidRequest,
            "profile configuration uses the supported schema"
        );
    }
    return {
        .profile_id = std::string(profile_id.value()),
        .detected_schema_version = detected_schema_version,
        .fingerprint = config::compute_config_fingerprint_from_bytes(
            config::current_configuration_fingerprint_version,
            path,
            bytes
        ),
        .managed_artifact_manifest_fingerprint =
            platform::linux::config::managed_artifact_manifest_fingerprint(
                profile_id,
                {roots_.etc_root, roots_.udev_root, roots_.systemd_root, roots_.public_root}
            ),
    };
}

EditableProfile SystemProfileAdministrationBackend::require_profile(const ProfileId& profile_id) const {
    auto profile = find_profile(profile_id);
    if (!profile.has_value())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile does not exist");
    return std::move(*profile);
}

config::Profile SystemProfileAdministrationBackend::parse_draft(
    const ProfileId& profile_id,
    const std::string& document
) const {
    if (document.size() > 1024U * 1024U)
        throw ValidationError("profile draft exceeds the supported size");
    config::Profile profile = [&] {
        try {
            return config::json::profile_from_json(config::json::Json::parse(document), target_mount_root_);
        } catch (const config::json::Json::exception& error) {
            throw ValidationError("profile draft is not valid JSON: " + std::string(error.what()));
        }
    }();
    if (profile.id != profile_id)
        throw ValidationError("profile draft identity does not match the request");
    profile.configuration_generation = config::ConfigurationGeneration{""};
    return profile;
}

ProfileDraftResult SystemProfileAdministrationBackend::validate_draft(
    const ProfileId& profile_id,
    const std::string& document
) const {
    const config::Profile profile = parse_draft(profile_id, document);
    return {
        .profile_id = std::string(profile_id.value()),
        .generation = {},
        .fingerprint = {},
        .document = config::json::dump_json(config::json::profile_to_json(profile)),
    };
}

void SystemProfileAdministrationBackend::require_current(const EditableProfile& expected) const {
    const auto current = find_profile(ProfileId{expected.profile_id});
    if (!current.has_value()) {
        if (expected.generation.empty() && expected.fingerprint.empty())
            return;
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "profile configuration changed");
    }
    if (current->generation != expected.generation || current->fingerprint != expected.fingerprint)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "profile configuration changed");
}

ProfileDraftResult SystemProfileAdministrationBackend::save_profile(
    const EditableProfile& expected,
    const ProfileDraftResult& draft,
    bool allow_hook_changes
) {
    require_current(expected);
    const ProfileId id(expected.profile_id);
    config::Profile profile = parse_draft(id, draft.document);
    const auto current = find_profile(id);
    const bool hooks_changed = current.has_value()
        ? !hooks_equal(platform::linux::config::FileProfileRepository(roots_.etc_root).get(id).profile.hooks, profile.hooks)
        : !hooks_empty(profile.hooks);
    if (!allow_hook_changes && hooks_changed) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotAuthorized,
            "hook changes require separate authorization"
        );
    }
    const platform::linux::config::ExpectedProfileIdentity expected_identity{
        current.has_value(),
        expected.generation,
        expected.fingerprint
    };
    platform::linux::config::install_profile(profile, {roots_.etc_root, roots_.udev_root, roots_.systemd_root, roots_.public_root}, activator_, &expected_identity);
    const EditableProfile saved = require_profile(id);
    return {
        .profile_id = saved.profile_id,
        .generation = saved.generation,
        .fingerprint = saved.fingerprint,
        .document = saved.document,
    };
}

void SystemProfileAdministrationBackend::delete_profile(const EditableProfile& expected) {
    require_current(expected);
    const ProfileId id(expected.profile_id);
    const config::Profile profile = platform::linux::config::FileProfileRepository(roots_.etc_root).get(id).profile;
    const platform::linux::config::ExpectedProfileIdentity expected_identity{
        true,
        expected.generation,
        expected.fingerprint
    };
    platform::linux::config::delete_profile(profile, {roots_.etc_root, roots_.udev_root, roots_.systemd_root, roots_.public_root}, activator_, &expected_identity);
}

void SystemProfileAdministrationBackend::retire_unsupported_profile(
    const UnsupportedProfileIdentity& expected
) {
    const ProfileId id(expected.profile_id);
    ProfileStateQuarantine quarantine(
        {roots_.state_root, roots_.status_root, roots_.history_root},
        expected.profile_id,
        expected.fingerprint
    );
    quarantine.quarantine();
    try {
        platform::linux::config::retire_unsupported_profile(
            id,
            expected.fingerprint,
            expected.managed_artifact_manifest_fingerprint,
            {roots_.etc_root, roots_.udev_root, roots_.systemd_root, roots_.public_root},
            activator_
        );
    } catch (...) {
        const auto rollback_result = quarantine.rollback();
        if (!rollback_result.complete) {
            throw platform::linux::config::ConfigurationSaveError(
                "unsupported profile retirement failed and state rollback was incomplete",
                rollback_result
            );
        }
        throw;
    }
    const ProfileStateQuarantineFinishResult finish_result = quarantine.finish();
    if (!finish_result.complete()) {
        std::fputs(
            "btrfs-backup: retired profile configuration, but transient status quarantine cleanup was incomplete\n",
            stderr
        );
    }
}

void SystemProfileAdministrationBackend::set_profile_enabled(const EditableProfile& expected, bool enabled) {
    require_current(expected);
    const ProfileId id(expected.profile_id);
    config::Profile profile = platform::linux::config::FileProfileRepository(roots_.etc_root).get(id).profile;
    if (profile.enabled == enabled)
        return;
    profile.enabled = enabled;
    profile.configuration_generation = config::ConfigurationGeneration{""};
    const ProfileDraftResult draft{
        .profile_id = expected.profile_id,
        .generation = {},
        .fingerprint = {},
        .document = config::json::dump_json(config::json::profile_to_json(profile)),
    };
    static_cast<void>(save_profile(expected, draft, false));
}

} // namespace btrfsbackup::daemon::control
