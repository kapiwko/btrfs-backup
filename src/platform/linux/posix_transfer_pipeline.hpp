// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>

#include <backup/transfer/transfer_pipeline.hpp>

namespace btrfsbackup {

struct TransferTerminationPolicy {
    std::chrono::milliseconds terminate_grace_period{5000};
    std::chrono::milliseconds kill_reap_period{5000};
};

class PosixTransferPipeline final : public ITransferPipeline {
  public:
    explicit PosixTransferPipeline(TransferTerminationPolicy termination_policy = {});

    TransferResult run(
        const TransferPipelinePlan& plan,
        ITransferEventSink& events,
        CancellationToken& cancellation
    ) override;

  private:
    TransferTerminationPolicy termination_policy_;
};

} // namespace btrfsbackup
