// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>

#include <backup/ports/ICommandRunner.hpp>
#include <backup/ports/TargetManager.hpp>

namespace btrfsbackup::platform::linux {

class SystemdMountedTargetSession final : public btrfsbackup::backup::IMountedTargetSession {
  public:
    SystemdMountedTargetSession(
        btrfsbackup::backup::ICommandRunner& commands,
        std::string mount_unit,
        bool mounted_by_this_session,
        std::string crypt_unit_to_restore = {}
    );
    ~SystemdMountedTargetSession() override;

    [[nodiscard]] bool mounted_by_this_session() const noexcept override;
    [[nodiscard]] std::optional<btrfsbackup::backup::TargetCleanupError> close() noexcept override;

  private:
    [[nodiscard]] std::optional<btrfsbackup::backup::TargetCleanupError> stop_unit(
        const std::string& unit,
        btrfsbackup::backup::TargetCleanupStage stage,
        const std::string& failure_prefix
    ) noexcept;

    btrfsbackup::backup::ICommandRunner& commands_;
    std::string mount_unit_;
    bool mounted_by_this_session_;
    std::string crypt_unit_to_restore_;
    bool closed_ = false;
    std::optional<btrfsbackup::backup::TargetCleanupError> close_error_;
};

} // namespace btrfsbackup::platform::linux
