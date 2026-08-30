// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/ports/ICommandRunner.hpp>
#include <backup/ports/IMountInspector.hpp>
#include <backup/ports/TargetManager.hpp>

namespace btrfsbackup::platform::linux::systemd {

class SystemdTargetManager final : public btrfsbackup::backup::ITargetManager {
  public:
    SystemdTargetManager(
        btrfsbackup::backup::IMountInspector& mounts,
        btrfsbackup::backup::ICommandRunner& commands,
        std::filesystem::path mapper_root = "/dev/mapper"
    );
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> prepare(
        const btrfsbackup::config::Profile& profile,
        btrfsbackup::backup::TargetMountMode mode
    ) override;

  private:
    btrfsbackup::backup::IMountInspector& mounts_;
    btrfsbackup::backup::ICommandRunner& commands_;
    std::filesystem::path mapper_root_;
};

} // namespace btrfsbackup::platform::linux::systemd
