// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/systemd_target_manager.hpp>

#include <memory>
#include <filesystem>
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
        bool mounted_by_this_session,
        std::string crypt_unit_to_restore = {}
    )
        : commands_(commands),
          mount_unit_(std::move(mount_unit)),
          mounted_by_this_session_(mounted_by_this_session),
          crypt_unit_to_restore_(std::move(crypt_unit_to_restore)) {
    }

    ~SystemdMountedTargetSession() override {
        if (!mounted_by_this_session_) {
            return;
        }
        try {
            const btrfsbackup::backup::CommandResult unmount = commands_.run(
                {"systemctl", "stop", mount_unit_}
            );
            if (unmount.exit_code == 0 && !crypt_unit_to_restore_.empty()) {
                (void)commands_.run({"systemctl", "stop", crypt_unit_to_restore_});
            }
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
    std::string crypt_unit_to_restore_;
};

} // namespace

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
        if (unmount.exit_code == 0 && !mapper_was_active) {
            (void)commands_.run({
                "systemctl",
                "stop",
                systemd_cryptsetup_unit_name(profile.target.mapper_name.value()),
            });
        }
        throw ValidationError("could not start target mount unit " + mount_unit);
    }
    return std::make_unique<SystemdMountedTargetSession>(
        commands_,
        mount_unit,
        true,
        mapper_was_active ? std::string{} : systemd_cryptsetup_unit_name(profile.target.mapper_name.value())
    );
}

} // namespace btrfsbackup::platform::linux
