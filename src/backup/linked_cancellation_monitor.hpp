// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/cancellation_monitor.hpp>
#include <core/cancellation.hpp>

namespace btrfsbackup::backup {

class LinkedCancellationMonitor final : public ICancellationMonitor {
  public:
    LinkedCancellationMonitor(ICancellationMonitor& primary, CancellationToken& upstream);

    [[nodiscard]] std::unique_ptr<ICancellationWatch> watch(
        const ProfileId& profile_id,
        CancellationToken& cancellation
    ) override;

  private:
    ICancellationMonitor& primary_;
    CancellationToken& upstream_;
};

} // namespace btrfsbackup::backup
