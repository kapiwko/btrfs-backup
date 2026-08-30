// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/ProfileRuntimePolicy.hpp>
#include <platform/linux/config/ProfileLegacyRuntimePolicy.hpp>

#include <string>

#include <config/model/ProfileDocument.hpp>
#include <config/domain/Validation.hpp>
#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <platform/linux/systemd/SystemdUnit.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

void validate_legacy_profile_runtime_fields(
    const btrfsbackup::config::Json& raw,
    const fs::path& target_mount_root
) {
    if (!raw.is_object() || !raw.contains("schemaVersion") || !raw.at("schemaVersion").is_number_integer()) {
        return;
    }
    const int schema_version = raw.at("schemaVersion").get<int>();
    if (schema_version >= btrfsbackup::config::current_profile_schema_version || !raw.contains("target") ||
        !raw.at("target").is_object() || !raw.contains("profileId") || !raw.at("profileId").is_string()) {
        return;
    }

    const std::string profile_id = raw.at("profileId").get<std::string>();
    validate_identifier(profile_id, "profileId");
    const fs::path mount_point =
        btrfsbackup::config::normalized_absolute_path(target_mount_root, "TARGET_MOUNT_ROOT") / profile_id;
    const btrfsbackup::config::Json& target = raw.at("target");
    if (target.contains("mountPoint")) {
        if (!target.at("mountPoint").is_string()) {
            throw ValidationError("target.mountPoint must be text");
        }
        if (btrfsbackup::config::normalized_absolute_path(
                target.at("mountPoint").get<std::string>(),
                "target.mountPoint"
            ) != mount_point) {
            throw ValidationError("legacy target.mountPoint does not match TARGET_MOUNT_ROOT/profileId");
        }
    }
    if (target.contains("mountUnit") && !target.at("mountUnit").is_null() && target.at("mountUnit") != "") {
        if (!target.at("mountUnit").is_string()) {
            throw ValidationError("target.mountUnit must be text");
        }
        if (target.at("mountUnit").get<std::string>() != systemd::systemd_mount_unit_name(mount_point)) {
            throw ValidationError("target.mountUnit does not match target.mountPoint");
        }
    }
}

void validate_profile_runtime_policy(const btrfsbackup::config::Profile& profile) {
    const auto validate_hooks = [](const std::vector<btrfsbackup::config::ProfileHookCommand>& hooks) {
        for (const btrfsbackup::config::ProfileHookCommand& hook : hooks) {
            const fs::path program = fs::path(hook.program).lexically_normal();
            if (program.parent_path() != fs::path(trusted_hook_directory) || program.filename().empty() ||
                program.filename() == "." || program.filename() == "..") {
                throw ValidationError(
                    "hook program must be a direct child of " + std::string(trusted_hook_directory)
                );
            }
        }
    };
    validate_hooks(profile.hooks.before_snapshot);
    validate_hooks(profile.hooks.after_snapshot);
}

} // namespace btrfsbackup::platform::linux
