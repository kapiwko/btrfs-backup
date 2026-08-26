// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/async_transfer.hpp>

#include <future>
#include <optional>
#include <utility>

namespace btrfsbackup {

namespace {

class ThreadedAsyncTransferHandle final : public IAsyncTransferHandle {
  public:
    ThreadedAsyncTransferHandle(
        std::shared_ptr<CancellationToken> cancellation,
        std::future<TransferResult> future
    ) : cancellation_(std::move(cancellation)),
        future_(std::move(future)) {}

    ~ThreadedAsyncTransferHandle() override {
        if (future_.valid() && !result_.has_value()) {
            request_cancel();
            try {
                result_ = future_.get();
            } catch (...) {
            }
        }
    }

    bool finished() const override {
        return result_.has_value() || wait_for(std::chrono::milliseconds(0));
    }

    bool wait_for(std::chrono::milliseconds timeout) const override {
        return result_.has_value() || future_.wait_for(timeout) == std::future_status::ready;
    }

    void request_cancel() override {
        cancellation_->request_cancel();
    }

    TransferResult wait() override {
        if (!result_.has_value()) {
            result_ = future_.get();
        }
        return *result_;
    }

  private:
    std::shared_ptr<CancellationToken> cancellation_;
    mutable std::future<TransferResult> future_;
    std::optional<TransferResult> result_;
};

} // namespace

ThreadedAsyncTransferPipeline::ThreadedAsyncTransferPipeline(ITransferPipeline& pipeline)
    : pipeline_(pipeline) {}

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

} // namespace btrfsbackup
