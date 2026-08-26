// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/backup_run_actions.hpp>
#include <backup/ports/trusted_executable.hpp>
#include <core/identifiers.hpp>
#include <core/cancellation.hpp>

namespace btrfsbackup {

class ICommandRunner;

class HookActionHandler {
  public:
    HookActionHandler(ICommandRunner& commands, const ITrustedExecutableResolver& executables);

    void handle(
        const RunHookAction& action,
        const ProfileId& profile_id,
        CancellationToken& cancellation
    );

  private:
    ICommandRunner& commands_;
    const ITrustedExecutableResolver& executables_;
};

} // namespace btrfsbackup
