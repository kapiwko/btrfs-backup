// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/PollingCancellationWatch.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <utility>

namespace btrfsbackup::state {

PollingCancellationWatch::PollingCancellationWatch(
    btrfsbackup::backup::ICancellationRequestStore& requests,
    btrfsbackup::backup::CancellationRequest request,
    CancellationToken& cancellation
)
    : requests_(requests), request_(std::move(request)), cancellation_(cancellation),
      worker_([this](std::stop_token stop) { run(stop); }) {
}

PollingCancellationWatch::~PollingCancellationWatch() {
    try {
        if (std::optional<std::string> diagnostic = close()) {
            std::clog << "btrfs-backup: cancellation watch cleanup failed: " << *diagnostic << '\n';
        }
    } catch (const std::exception& error) {
        std::clog << "btrfs-backup: cancellation watch cleanup failed: " << error.what() << '\n';
    } catch (...) {
        std::clog << "btrfs-backup: cancellation watch cleanup failed with an unknown error\n";
    }
}

std::optional<std::string> PollingCancellationWatch::close() {
    if (closed_) {
        return std::nullopt;
    }
    closed_ = true;
    worker_.request_stop();
    try {
        if (worker_.joinable()) {
            worker_.join();
        }
        return std::nullopt;
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "unknown cancellation watch cleanup failure";
    }
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
