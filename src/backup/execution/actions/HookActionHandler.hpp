// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/BackupRunActions.hpp>
#include <backup/ports/TrustedExecutable.hpp>
#include <core/Identifiers.hpp>
#include <core/Cancellation.hpp>

namespace btrfsbackup::backup {
class ICommandRunner;
}

namespace btrfsbackup::backup::execution {

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

} // namespace btrfsbackup::backup::execution
