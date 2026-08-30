// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>

#include <backup/transfer/ITransferPipeline.hpp>

namespace btrfsbackup::platform::linux::transfer {

struct TransferTerminationPolicy {
    std::chrono::milliseconds terminate_grace_period{5000};
    std::chrono::milliseconds kill_reap_period{5000};
};

class PosixTransferPipeline final : public btrfsbackup::backup::transfer::ITransferPipeline {
  public:
    explicit PosixTransferPipeline(TransferTerminationPolicy termination_policy = {});

    [[nodiscard]] btrfsbackup::backup::transfer::TransferResult run(
        const btrfsbackup::backup::transfer::TransferPipelinePlan& plan,
        btrfsbackup::backup::transfer::ITransferEventSink& events,
        CancellationToken& cancellation
    ) override;

  private:
    TransferTerminationPolicy termination_policy_;
};

} // namespace btrfsbackup::platform::linux::transfer
