// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/SystemdMountedTargetSession.hpp>

#include <exception>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_destructible_v<btrfsbackup::platform::linux::SystemdMountedTargetSession>);

namespace btrfsbackup::platform::linux {

SystemdMountedTargetSession::SystemdMountedTargetSession(
    btrfsbackup::backup::ICommandRunner& commands,
    std::string mount_unit,
    bool mounted_by_this_session,
    std::string crypt_unit_to_restore
)
    : commands_(commands),
      mount_unit_(std::move(mount_unit)),
      mounted_by_this_session_(mounted_by_this_session),
      crypt_unit_to_restore_(std::move(crypt_unit_to_restore)) {
}

SystemdMountedTargetSession::~SystemdMountedTargetSession() noexcept {
    (void)close();
}

bool SystemdMountedTargetSession::mounted_by_this_session() const noexcept {
    return mounted_by_this_session_;
}

std::optional<btrfsbackup::backup::TargetCleanupError> SystemdMountedTargetSession::close() noexcept {
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

std::optional<btrfsbackup::backup::TargetCleanupError> SystemdMountedTargetSession::stop_unit(
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

} // namespace btrfsbackup::platform::linux
