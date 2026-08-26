// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/systemd_target_mounter.hpp>

#include <string>

#include <core/errors.hpp>

namespace btrfsbackup {

SystemdTargetMounter::SystemdTargetMounter(IMountInspector& mounts, ICommandRunner& commands)
    : mounts_(mounts), commands_(commands) {
}

void SystemdTargetMounter::ensure_mounted(const Profile& profile) {
    if (mount_at(mounts_.inspect(), profile.target.mount_point).has_value()) {
        return;
    }
    if (profile.target.mount_unit.empty()) {
        throw ValidationError("target.mountUnit is required to mount backup target");
    }
    const CommandResult result = commands_.run({"systemctl", "start", profile.target.mount_unit});
    if (result.exit_code != 0) {
        throw ValidationError("could not start target mount unit " + profile.target.mount_unit);
    }
}

} // namespace btrfsbackup
