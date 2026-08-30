// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/systemd/SystemdTargetManager.hpp>
#include <platform/linux/systemd/SystemdMountedTargetSession.hpp>

#include <memory>
#include <filesystem>
#include <string>
#include <utility>

#include <core/Errors.hpp>
#include <platform/linux/systemd/SystemdUnit.hpp>

namespace btrfsbackup::platform::linux::systemd {

SystemdTargetManager::SystemdTargetManager(
    btrfsbackup::backup::IMountInspector& mounts,
    btrfsbackup::backup::ICommandRunner& commands,
    std::filesystem::path mapper_root
)
    : mounts_(mounts), commands_(commands), mapper_root_(std::move(mapper_root)) {
}

std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> SystemdTargetManager::prepare(
    const btrfsbackup::config::Profile& profile,
    btrfsbackup::backup::TargetMountMode mode
) {
    const std::string mount_unit = systemd_mount_unit_name(profile.target.mount_point);
    if (btrfsbackup::backup::mount_at(mounts_.inspect(), profile.target.mount_point).has_value()) {
        return std::make_unique<SystemdMountedTargetSession>(commands_, mount_unit, false);
    }
    if (mode == btrfsbackup::backup::TargetMountMode::RequireMounted) {
        return std::make_unique<SystemdMountedTargetSession>(commands_, mount_unit, false);
    }
    const bool mapper_was_active = std::filesystem::exists(
        mapper_root_ / profile.target.mapper_name.value()
    );
    const btrfsbackup::backup::CommandResult result = commands_.run({"systemctl", "start", mount_unit});
    if (result.exit_code != 0) {
        const btrfsbackup::backup::CommandResult unmount = commands_.run(
            {"systemctl", "stop", mount_unit}
        );
        if (unmount.exit_code != 0) {
            throw ValidationError(
                "could not start target mount unit " + mount_unit +
                "; could not stop target mount unit during rollback (exit code " +
                std::to_string(unmount.exit_code) + ")"
            );
        }
        if (!mapper_was_active) {
            const std::string crypt_unit = target_activation_unit_name(profile.id.value());
            const btrfsbackup::backup::CommandResult crypt_stop = commands_.run({
                "systemctl",
                "stop",
                crypt_unit,
            });
            if (crypt_stop.exit_code != 0) {
                throw ValidationError(
                    "could not start target mount unit " + mount_unit +
                    "; could not stop target cryptsetup unit during rollback " + crypt_unit +
                    " (exit code " + std::to_string(crypt_stop.exit_code) + ")"
                );
            }
        }
        throw ValidationError("could not start target mount unit " + mount_unit);
    }
    return std::make_unique<SystemdMountedTargetSession>(
        commands_,
        mount_unit,
        true,
        mapper_was_active ? std::string{} : target_activation_unit_name(profile.id.value())
    );
}

} // namespace btrfsbackup::platform::linux::systemd
