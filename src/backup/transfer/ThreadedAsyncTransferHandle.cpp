// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/ThreadedAsyncTransferHandle.hpp>

#include <utility>

namespace btrfsbackup::backup::transfer {

ThreadedAsyncTransferHandle::ThreadedAsyncTransferHandle(
    std::shared_ptr<CancellationToken> cancellation,
    std::future<TransferResult> future
) : cancellation_(std::move(cancellation)),
    future_(std::move(future)) {
}

ThreadedAsyncTransferHandle::~ThreadedAsyncTransferHandle() noexcept {
    if (future_.valid() && !result_.has_value()) {
        request_cancel();
        try {
            result_ = future_.get();
        } catch (...) {
        }
    }
}

bool ThreadedAsyncTransferHandle::finished() const {
    return result_.has_value() || wait_for(std::chrono::milliseconds(0));
}

bool ThreadedAsyncTransferHandle::wait_for(std::chrono::milliseconds timeout) const {
    return result_.has_value() || future_.wait_for(timeout) == std::future_status::ready;
}

void ThreadedAsyncTransferHandle::request_cancel() {
    cancellation_->request_cancel();
}

TransferResult ThreadedAsyncTransferHandle::wait() {
    if (!result_.has_value()) {
        result_ = future_.get();
    }
    return *result_;
}

} // namespace btrfsbackup::backup::transfer
