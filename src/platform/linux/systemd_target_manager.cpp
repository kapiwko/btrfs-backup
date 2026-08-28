// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/systemd_target_manager.hpp>

#include <string>

#include <core/errors.hpp>
#include <platform/linux/systemd_unit.hpp>

namespace btrfsbackup::platform::linux {

SystemdTargetManager::SystemdTargetManager(btrfsbackup::backup::IMountInspector& mounts, btrfsbackup::backup::ICommandRunner& commands)
    : mounts_(mounts), commands_(commands) {
}

void SystemdTargetManager::ensure_mounted(const btrfsbackup::config::Profile& profile) {
    if (btrfsbackup::backup::mount_at(mounts_.inspect(), profile.target.mount_point).has_value()) {
        return;
    }
    const std::string mount_unit = systemd_mount_unit_name(profile.target.mount_point);
    const btrfsbackup::backup::CommandResult result = commands_.run({"systemctl", "start", mount_unit});
    if (result.exit_code != 0) {
        throw ValidationError("could not start target mount unit " + mount_unit);
    }
}

} // namespace btrfsbackup::platform::linux
