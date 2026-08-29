// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/systemd_target_manager.hpp>

#include <memory>
#include <filesystem>
#include <exception>
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
        (void)close();
    }

    bool mounted_by_this_session() const noexcept override {
        return mounted_by_this_session_;
    }

    std::optional<btrfsbackup::backup::TargetCleanupError> close() noexcept override {
        if (closed_) {
            return close_error_;
        }
        closed_ = true;
        if (!mounted_by_this_session_) {
            return std::nullopt;
        }

        close_error_ = stop_unit(
            mount_unit_,
            btrfsbackup::backup::TargetCleanupStage::MountUnit,
            "could not stop target mount unit "
        );
        if (close_error_.has_value() || crypt_unit_to_restore_.empty()) {
            return close_error_;
        }
        close_error_ = stop_unit(
            crypt_unit_to_restore_,
            btrfsbackup::backup::TargetCleanupStage::CryptsetupUnit,
            "could not stop target cryptsetup unit "
        );
        return close_error_;
    }

  private:
    std::optional<btrfsbackup::backup::TargetCleanupError> stop_unit(
        const std::string& unit,
        btrfsbackup::backup::TargetCleanupStage stage,
        const std::string& failure_prefix
    ) noexcept {
        try {
            const btrfsbackup::backup::CommandResult result = commands_.run({"systemctl", "stop", unit});
            if (result.exit_code == 0) {
                return std::nullopt;
            }
            std::string message = failure_prefix + unit + " (exit code " + std::to_string(result.exit_code) + ")";
            if (!result.output.empty()) {
                message += ": " + result.output;
            }
            return btrfsbackup::backup::TargetCleanupError{stage, unit, result.exit_code, std::move(message)};
        } catch (const std::exception& error) {
            return btrfsbackup::backup::TargetCleanupError{
                stage,
                unit,
                -1,
                failure_prefix + unit + ": " + error.what(),
            };
        } catch (...) {
            return btrfsbackup::backup::TargetCleanupError{
                stage,
                unit,
                -1,
                failure_prefix + unit + ": unknown error",
            };
        }
    }

    btrfsbackup::backup::ICommandRunner& commands_;
    std::string mount_unit_;
    bool mounted_by_this_session_;
    std::string crypt_unit_to_restore_;
    bool closed_ = false;
    std::optional<btrfsbackup::backup::TargetCleanupError> close_error_;
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

} // namespace btrfsbackup::platform::linux
