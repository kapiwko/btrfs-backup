// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/CancellationMonitor.hpp>
#include <backup/ports/CancellationRequestStore.hpp>

namespace btrfsbackup::state {

class FileCancellationMonitor final : public btrfsbackup::backup::ICancellationMonitor {
  public:
    explicit FileCancellationMonitor(btrfsbackup::backup::ICancellationRequestStore& requests);

    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ICancellationWatch> watch(
        const btrfsbackup::backup::CancellationRequest& request,
        CancellationToken& cancellation
    ) override;

  private:
    btrfsbackup::backup::ICancellationRequestStore& requests_;
};

} // namespace btrfsbackup::state
