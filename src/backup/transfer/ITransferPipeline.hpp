// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/transfer/TransferEvent.hpp>
#include <backup/transfer/TransferPlan.hpp>
#include <backup/transfer/TransferResult.hpp>
#include <core/Cancellation.hpp>

namespace btrfsbackup::backup::transfer {

class ITransferPipeline {
  public:
    virtual ~ITransferPipeline() = default;
    [[nodiscard]] virtual TransferResult run(
        const TransferPipelinePlan& plan,
        ITransferEventSink& events,
        CancellationToken& cancellation
    ) = 0;
};

} // namespace btrfsbackup::backup::transfer
