// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/AsyncTransfer.hpp>
#include <backup/transfer/ThreadedAsyncTransferHandle.hpp>

#include <future>
#include <utility>

namespace btrfsbackup::backup::transfer {

ThreadedAsyncTransferPipeline::ThreadedAsyncTransferPipeline(ITransferPipeline& pipeline)
    : pipeline_(pipeline) {
}

std::unique_ptr<IAsyncTransferHandle> ThreadedAsyncTransferPipeline::start(
    const TransferPipelinePlan& plan,
    ITransferEventSink& events
) {
    auto cancellation = std::make_shared<CancellationToken>();
    std::future<TransferResult> future = std::async(
        std::launch::async,
        [this, plan, &events, cancellation] {
            return pipeline_.run(plan, events, *cancellation);
        }
    );
    return std::make_unique<ThreadedAsyncTransferHandle>(std::move(cancellation), std::move(future));
}

} // namespace btrfsbackup::backup::transfer
