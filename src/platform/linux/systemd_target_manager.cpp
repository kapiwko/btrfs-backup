// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/systemd_target_manager.hpp>

#include <memory>
#include <string>
#include <utility>

#include <core/errors.hpp>
#include <platform/linux/systemd_unit.hpp>

namespace btrfsbackup::platform::linux {

namespace {

class SystemdMountedTargetSession final : public btrfsbackup::backup::IMountedTargetSession {
  public:
    SystemdMountedTargetSession(
        btrfsbackup::backup::ICommandRunner& commands,
        std::string mount_unit,
        bool mounted_by_this_session
    )
        : commands_(commands),
          mount_unit_(std::move(mount_unit)),
          mounted_by_this_session_(mounted_by_this_session) {
    }

    ~SystemdMountedTargetSession() override {
        if (!mounted_by_this_session_) {
            return;
        }
        try {
            (void)commands_.run({"systemctl", "stop", mount_unit_});
        } catch (...) {
        }
    }

    bool mounted_by_this_session() const noexcept override {
        return mounted_by_this_session_;
    }

  private:
    btrfsbackup::backup::ICommandRunner& commands_;
    std::string mount_unit_;
    bool mounted_by_this_session_;
};

} // namespace

SystemdTargetManager::SystemdTargetManager(btrfsbackup::backup::IMountInspector& mounts, btrfsbackup::backup::ICommandRunner& commands)
    : mounts_(mounts), commands_(commands) {
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
    const btrfsbackup::backup::CommandResult result = commands_.run({"systemctl", "start", mount_unit});
    if (result.exit_code != 0) {
        throw ValidationError("could not start target mount unit " + mount_unit);
    }
    return std::make_unique<SystemdMountedTargetSession>(commands_, mount_unit, true);
}

} // namespace btrfsbackup::platform::linux
