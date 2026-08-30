// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <memory>

#include <backup/transfer/ITransferPipeline.hpp>

namespace btrfsbackup::backup::transfer {

class IAsyncTransferHandle {
  public:
    virtual ~IAsyncTransferHandle() = default;
    [[nodiscard]] virtual bool finished() const = 0;
    [[nodiscard]] virtual bool wait_for(std::chrono::milliseconds timeout) const = 0;
    virtual void request_cancel() = 0;
    [[nodiscard]] virtual TransferResult wait() = 0;
};

class IAsyncTransferPipeline {
  public:
    virtual ~IAsyncTransferPipeline() = default;
    [[nodiscard]] virtual std::unique_ptr<IAsyncTransferHandle> start(
        const TransferPipelinePlan& plan,
        ITransferEventSink& events
    ) = 0;
};

class ThreadedAsyncTransferPipeline final : public IAsyncTransferPipeline {
  public:
    explicit ThreadedAsyncTransferPipeline(ITransferPipeline& pipeline);

    [[nodiscard]] std::unique_ptr<IAsyncTransferHandle> start(
        const TransferPipelinePlan& plan,
        ITransferEventSink& events
    ) override;

  private:
    ITransferPipeline& pipeline_;
};

} // namespace btrfsbackup::backup::transfer
