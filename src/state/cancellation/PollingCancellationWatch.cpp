// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/cancellation/PollingCancellationWatch.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_destructible_v<btrfsbackup::state::PollingCancellationWatch>);

namespace btrfsbackup::state {

PollingCancellationWatch::PollingCancellationWatch(
    btrfsbackup::backup::ICancellationRequestStore& requests,
    btrfsbackup::backup::CancellationRequest request,
    CancellationToken& cancellation
)
    : requests_(requests), request_(std::move(request)), cancellation_(cancellation),
      worker_([this](std::stop_token stop) { run(stop); }) {
}

PollingCancellationWatch::~PollingCancellationWatch() noexcept {
    if (const auto& diagnostic = close()) {
        std::clog << "btrfs-backup: cancellation watch cleanup failed: " << diagnostic->message << '\n';
    }
}

const std::optional<btrfsbackup::backup::CleanupDiagnostic>& PollingCancellationWatch::close() noexcept {
    if (closed_) {
        return close_diagnostic_;
    }
    closed_ = true;
    worker_.request_stop();
    try {
        if (worker_.joinable()) {
            worker_.join();
        }
    } catch (const std::exception& error) {
        close_diagnostic_ = btrfsbackup::backup::CleanupDiagnostic{error.what()};
    } catch (...) {
        close_diagnostic_ = btrfsbackup::backup::CleanupDiagnostic{"unknown cancellation watch cleanup failure"};
    }
    return close_diagnostic_;
}

void PollingCancellationWatch::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        if (requests_.cancel_requested(request_)) {
            cancellation_.request_cancel();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace btrfsbackup::state
