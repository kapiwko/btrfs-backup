// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemProfileAdministrationBackend.hpp>

#include <config/json/JsonIo.hpp>
#include <config/json/ProfileDocument.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <core/Errors.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/config/ProfileService.hpp>
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

std::vector<std::filesystem::path> SystemProfileAdministrationBackend::source_candidates() const {
    try {
        const auto targets = platform::linux::storage::btrfs_mount_targets(mountinfo_path_);
        return {targets.begin(), targets.end()};
    } catch (...) {
        return {};
    }
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
        current.has_value(), expected.generation, expected.fingerprint
    };
    platform::linux::config::install_profile(profile, {
        roots_.etc_root, roots_.udev_root, roots_.systemd_root, roots_.public_root
    }, activator_, &expected_identity);
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
        true, expected.generation, expected.fingerprint
    };
    platform::linux::config::delete_profile(profile, {
        roots_.etc_root, roots_.udev_root, roots_.systemd_root, roots_.public_root
    }, activator_, &expected_identity);
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
