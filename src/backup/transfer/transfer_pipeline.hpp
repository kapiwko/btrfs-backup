// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/transfer/transfer_event.hpp>
#include <backup/transfer/transfer_plan.hpp>
#include <backup/transfer/transfer_result.hpp>
#include <core/cancellation.hpp>

namespace btrfsbackup {

class ITransferPipeline {
  public:
    virtual ~ITransferPipeline() = default;
    [[nodiscard]] virtual TransferResult run(
        const TransferPipelinePlan& plan,
        ITransferEventSink& events,
        CancellationToken& cancellation
    ) = 0;
};

} // namespace btrfsbackup
