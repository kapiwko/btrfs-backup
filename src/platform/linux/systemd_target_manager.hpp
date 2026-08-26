// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/command_runner.hpp>
#include <backup/ports/mount_inspector.hpp>
#include <backup/ports/target_manager.hpp>

namespace btrfsbackup {

class SystemdTargetManager final : public ITargetManager {
  public:
    SystemdTargetManager(IMountInspector& mounts, ICommandRunner& commands);
    void ensure_mounted(const Profile& profile) override;

  private:
    IMountInspector& mounts_;
    ICommandRunner& commands_;
};

} // namespace btrfsbackup
