#include <string>
#include <vector>

#include <btrfsbackup/transfer_pipeline.hpp>

#include "test_helpers.hpp"

namespace {

class RecordingEventSink final : public btrfsbackup::ITransferEventSink {
public:
    std::vector<btrfsbackup::TransferEvent> events;

    void on_transfer_event(const btrfsbackup::TransferEvent& event) override {
        events.push_back(event);
    }
};

void test_cancellation_token() {
    btrfsbackup::CancellationToken cancellation;
    test_helpers::expect_true("initial cancellation", !cancellation.cancellation_requested(), "token should not start cancelled");
    cancellation.request_cancel();
    test_helpers::expect_true("requested cancellation", cancellation.cancellation_requested(), "token should report cancellation");
}

void test_success_result() {
    btrfsbackup::TransferResult result{
        .producer = {.started = true, .exit_code = 0},
        .consumer = {.started = true, .exit_code = 0},
        .bytes_transferred = 123,
    };

    test_helpers::expect_true("transfer success", btrfsbackup::transfer_succeeded(result), "successful sides should succeed");
    btrfsbackup::require_transfer_success(result);
}

void test_producer_failure_is_reported_separately() {
    btrfsbackup::TransferResult result{
        .producer = {.started = true, .exit_code = 1, .diagnostics = "send failed"},
        .consumer = {.started = true, .exit_code = 0},
    };

    test_helpers::expect_validation_error("producer failure", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "producer failed with exit code 1: send failed");
}

void test_consumer_failure_is_reported_separately() {
    btrfsbackup::TransferResult result{
        .producer = {.started = true, .exit_code = 0},
        .consumer = {.started = true, .exit_code = 1, .diagnostics = "receive failed"},
    };

    test_helpers::expect_validation_error("consumer failure", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "consumer failed with exit code 1: receive failed");
}

void test_both_sides_failure_keeps_both_diagnostics() {
    btrfsbackup::TransferResult result{
        .producer = {.started = true, .exit_code = 1, .diagnostics = "send failed"},
        .consumer = {.started = true, .exit_code = 2, .diagnostics = "receive failed"},
    };

    test_helpers::expect_validation_error("both sides failure", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "producer failed with exit code 1: send failed; consumer failed with exit code 2: receive failed");
}

void test_cancelled_transfer_is_reported() {
    btrfsbackup::TransferResult result{
        .producer = {.started = true, .exit_code = 0},
        .consumer = {.started = true, .exit_code = 0},
        .cancelled = true,
    };

    test_helpers::expect_validation_error("cancelled transfer", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "Transfer was cancelled");
}

void test_event_sink_contract() {
    RecordingEventSink sink;
    sink.on_transfer_event({
        .kind = btrfsbackup::TransferEventKind::Progress,
        .bytes_transferred = 4096,
        .message = "chunk",
    });

    test_helpers::expect_eq("event count", std::to_string(sink.events.size()), "1");
    test_helpers::expect_eq("event bytes", std::to_string(sink.events.at(0).bytes_transferred), "4096");
    test_helpers::expect_eq("event message", sink.events.at(0).message, "chunk");
}

} // namespace

int main() {
    test_cancellation_token();
    test_success_result();
    test_producer_failure_is_reported_separately();
    test_consumer_failure_is_reported_separately();
    test_both_sides_failure_keeps_both_diagnostics();
    test_cancelled_transfer_is_reported();
    test_event_sink_contract();

    return test_helpers::finish("transfer pipeline tests");
}
