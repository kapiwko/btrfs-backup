// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/command_runner.hpp>
#include <backup/ports/mount_inspector.hpp>
#include <backup/ports/target_manager.hpp>

namespace btrfsbackup::platform::linux {

class SystemdTargetManager final : public btrfsbackup::backup::ITargetManager {
  public:
    SystemdTargetManager(btrfsbackup::backup::IMountInspector& mounts, btrfsbackup::backup::ICommandRunner& commands);
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::IMountedTargetSession> prepare(
        const btrfsbackup::config::Profile& profile,
        btrfsbackup::backup::TargetMountMode mode
    ) override;

  private:
    btrfsbackup::backup::IMountInspector& mounts_;
    btrfsbackup::backup::ICommandRunner& commands_;
};

} // namespace btrfsbackup::platform::linux
