// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <future>
#include <memory>
#include <optional>

#include <backup/transfer/AsyncTransfer.hpp>

namespace btrfsbackup::backup::transfer {

class ThreadedAsyncTransferHandle final : public IAsyncTransferHandle {
  public:
    ThreadedAsyncTransferHandle(
        std::shared_ptr<CancellationToken> cancellation,
        std::future<TransferResult> future
    );
    ~ThreadedAsyncTransferHandle() override;

    [[nodiscard]] bool finished() const override;
    [[nodiscard]] bool wait_for(std::chrono::milliseconds timeout) const override;
    void request_cancel() override;
    TransferResult wait() override;

  private:
    std::shared_ptr<CancellationToken> cancellation_;
    mutable std::future<TransferResult> future_;
    std::optional<TransferResult> result_;
};

} // namespace btrfsbackup::backup::transfer
