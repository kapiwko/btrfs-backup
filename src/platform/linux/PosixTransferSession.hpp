// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/transfer/ITransferPipeline.hpp>
#include <platform/linux/PosixTransferPipeline.hpp>

namespace btrfsbackup::platform::linux {

class PosixTransferSession final {
  public:
    PosixTransferSession(
        const btrfsbackup::backup::transfer::TransferPipelinePlan& plan,
        btrfsbackup::backup::transfer::ITransferEventSink& events,
        CancellationToken& cancellation,
        TransferTerminationPolicy termination_policy
    );

    [[nodiscard]] btrfsbackup::backup::transfer::TransferResult run();

  private:
    const btrfsbackup::backup::transfer::TransferPipelinePlan& plan_;
    btrfsbackup::backup::transfer::ITransferEventSink& events_;
    CancellationToken& cancellation_;
    TransferTerminationPolicy termination_policy_;
};

} // namespace btrfsbackup::platform::linux
