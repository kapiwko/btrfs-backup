// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <state/cancellation/FileCancellationMonitor.hpp>

#include "support/TestHelpers.hpp"

namespace {

class RecordingCancellationRequests final : public btrfsbackup::backup::ICancellationRequestStore {
  public:
    std::atomic<bool> requested = false;
    mutable std::atomic<int> checks = 0;

    std::unique_ptr<btrfsbackup::backup::IActiveRunRegistration> register_active_run(
        const btrfsbackup::backup::CancellationRequest&
    ) override {
        return nullptr;
    }

    btrfsbackup::backup::CancellationRequestOutcome request_cancel(
        const btrfsbackup::backup::CancellationRequest&
    ) override {
        return btrfsbackup::backup::CancellationRequestOutcome::Accepted;
    }

    bool cancel_requested(const btrfsbackup::backup::CancellationRequest&) const override {
        ++checks;
        return requested.load();
    }

    void clear_cancel_request(const btrfsbackup::backup::CancellationRequest&) override {
    }
};

btrfsbackup::backup::CancellationRequest request() {
    return {
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"run-1"},
    };
}

void wait_for_check(const RecordingCancellationRequests& requests) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (requests.checks.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void test_close_is_idempotent_and_joins_worker() {
    RecordingCancellationRequests requests;
    btrfsbackup::state::FileCancellationMonitor monitor(requests);
    btrfsbackup::CancellationToken cancellation;
    std::unique_ptr<btrfsbackup::backup::ICancellationWatch> watch = monitor.watch(request(), cancellation);
    wait_for_check(requests);

    test_helpers::expect_true("watch started", requests.checks.load() > 0, "watch did not poll the request store");
    test_helpers::expect_true("first close clean", !watch->close().has_value(), "first close returned a diagnostic");
    const int checks_after_close = requests.checks.load();
    test_helpers::expect_true("second close clean", !watch->close().has_value(), "second close returned a diagnostic");
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    test_helpers::expect_eq(
        "worker joined",
        std::to_string(requests.checks.load()),
        std::to_string(checks_after_close)
    );
}

void test_request_propagates_to_cancellation_token() {
    RecordingCancellationRequests requests;
    requests.requested = true;
    btrfsbackup::state::FileCancellationMonitor monitor(requests);
    btrfsbackup::CancellationToken cancellation;
    std::unique_ptr<btrfsbackup::backup::ICancellationWatch> watch = monitor.watch(request(), cancellation);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!cancellation.cancellation_requested() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    test_helpers::expect_true(
        "cancellation propagated",
        cancellation.cancellation_requested(),
        "watch did not request cancellation"
    );
    test_helpers::expect_true("completed watch close", !watch->close().has_value(), "completed watch did not close cleanly");
}

} // namespace

int main() {
    test_close_is_idempotent_and_joins_worker();
    test_request_propagates_to_cancellation_token();
    return test_helpers::finish("cancellation watch lifecycle tests");
}
