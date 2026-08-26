// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/backup_run_actions.hpp>
#include <backup/ports/trusted_executable.hpp>
#include <core/identifiers.hpp>
#include <core/cancellation.hpp>

namespace btrfsbackup {

class ICommandRunner;

class HookActionHandler {
  public:
    explicit HookActionHandler(ICommandRunner& commands);
    HookActionHandler(
        ICommandRunner& commands,
        std::filesystem::path hook_root,
        TrustedExecutablePolicy hook_policy
    );

    void handle(
        const RunHookAction& action,
        const ProfileId& profile_id,
        CancellationToken& cancellation
    );

  private:
    ICommandRunner& commands_;
    std::filesystem::path hook_root_;
    TrustedExecutablePolicy hook_policy_;
};

} // namespace btrfsbackup
