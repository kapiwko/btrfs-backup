// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <backup/ports/TargetManager.hpp>
#include <config/model/Profile.hpp>
#include <core/Cancellation.hpp>

namespace btrfsbackup::backup {

class IBackupPreflight {
  public:
    virtual ~IBackupPreflight() = default;

    [[nodiscard]] virtual std::unique_ptr<IMountedTargetSession> run(
        const btrfsbackup::config::Profile& profile,
        TargetMountMode mode,
        CancellationToken& cancellation
    ) = 0;
};

} // namespace btrfsbackup::backup
