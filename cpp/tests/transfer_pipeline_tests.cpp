#include <poll.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>
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

class BlockingTransferPipeline final : public btrfsbackup::ITransferPipeline {
public:
    std::atomic_bool entered = false;
    std::atomic_bool allow_finish = false;

    btrfsbackup::TransferResult run(
        const btrfsbackup::TransferPipelinePlan&,
        btrfsbackup::ITransferEventSink&,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        entered.store(true);
        while (!allow_finish.load() && !cancellation.cancellation_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return {
            .producer = {.started = true, .exit_code = 0},
            .consumer = {.started = true, .exit_code = 0},
            .cancelled = cancellation.cancellation_requested(),
        };
    }
};

void wait_until_entered(BlockingTransferPipeline& pipeline) {
    for (int i = 0; i < 100 && !pipeline.entered.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    test_helpers::expect_true("async pipeline entered", pipeline.entered.load(), "pipeline did not start");
}

void test_cancellation_token() {
    btrfsbackup::CancellationToken cancellation;
    test_helpers::expect_true("initial cancellation", !cancellation.cancellation_requested(), "token should not start cancelled");
    cancellation.request_cancel();
    test_helpers::expect_true("requested cancellation", cancellation.cancellation_requested(), "token should report cancellation");
}

void test_cancellation_token_signals_fd() {
    btrfsbackup::CancellationToken cancellation;
    pollfd fd{
        .fd = cancellation.cancellation_fd(),
        .events = POLLIN,
        .revents = 0,
    };

    test_helpers::expect_eq("initial cancellation poll", std::to_string(poll(&fd, 1, 0)), "0");
    cancellation.request_cancel();
    test_helpers::expect_eq("requested cancellation poll", std::to_string(poll(&fd, 1, 100)), "1");
    cancellation.drain_cancellation_signal();
    fd.revents = 0;
    test_helpers::expect_eq("drained cancellation poll", std::to_string(poll(&fd, 1, 0)), "0");
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

    test_helpers::expect_eq("producer error code", btrfsbackup::transfer_failure_error_code(result), "transfer.producer_failed");
    test_helpers::expect_validation_error("producer failure", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "producer failed with exit code 1: send failed");
}

void test_consumer_failure_is_reported_separately() {
    btrfsbackup::TransferResult result{
        .producer = {.started = true, .exit_code = 0},
        .consumer = {.started = true, .exit_code = 1, .diagnostics = "receive failed"},
    };

    test_helpers::expect_eq("consumer error code", btrfsbackup::transfer_failure_error_code(result), "transfer.consumer_failed");
    test_helpers::expect_validation_error("consumer failure", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "consumer failed with exit code 1: receive failed");
}

void test_both_sides_failure_keeps_both_diagnostics() {
    btrfsbackup::TransferResult result{
        .producer = {.started = true, .exit_code = 1, .diagnostics = "send failed"},
        .consumer = {.started = true, .exit_code = 2, .diagnostics = "receive failed"},
    };

    test_helpers::expect_eq("both sides error code", btrfsbackup::transfer_failure_error_code(result), "transfer.producer_consumer_failed");
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

void test_posix_pipeline_transfers_bytes() {
    btrfsbackup::PosixTransferPipeline pipeline;
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {"printf", "hello"},
            .consumer_argv = {"cat"},
        },
        sink,
        cancellation
    );

    test_helpers::expect_true("posix transfer success", btrfsbackup::transfer_succeeded(result), "pipeline should succeed");
    test_helpers::expect_eq("posix transfer bytes", std::to_string(result.bytes_transferred), "5");
    test_helpers::expect_eq("posix produced bytes", std::to_string(result.bytes_produced), "5");
    test_helpers::expect_true("posix duration", result.duration_ms >= 0, "pipeline should report duration");
    test_helpers::expect_true("posix transfer events", sink.events.size() >= 3, "pipeline should emit lifecycle events");
    auto progress = std::find_if(sink.events.begin(), sink.events.end(), [](const btrfsbackup::TransferEvent& event) {
        return event.kind == btrfsbackup::TransferEventKind::Progress;
    });
    test_helpers::expect_true("posix progress event", progress != sink.events.end(), "pipeline should emit progress");
    if (progress != sink.events.end()) {
        test_helpers::expect_eq("posix progress delta", std::to_string(progress->delta_bytes), "5");
        test_helpers::expect_eq("posix progress written", std::to_string(progress->bytes_transferred), "5");
        test_helpers::expect_eq("posix progress produced", std::to_string(progress->bytes_produced), "5");
    }
    test_helpers::expect_eq(
        "posix final event",
        std::to_string(static_cast<int>(sink.events.back().kind)),
        std::to_string(static_cast<int>(btrfsbackup::TransferEventKind::Completed))
    );
}

void test_posix_pipeline_reports_producer_failure() {
    btrfsbackup::PosixTransferPipeline pipeline;
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {"sh", "-c", "echo producer-error >&2; exit 7"},
            .consumer_argv = {"cat"},
        },
        sink,
        cancellation
    );

    test_helpers::expect_eq("producer exit", std::to_string(result.producer.exit_code), "7");
    test_helpers::expect_eq("consumer exit", std::to_string(result.consumer.exit_code), "0");
    test_helpers::expect_contains("producer diagnostics", result.producer.diagnostics, "producer-error");
    test_helpers::expect_validation_error("producer failure result", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "producer failed with exit code 7");
}

void test_posix_pipeline_reports_missing_producer() {
    btrfsbackup::PosixTransferPipeline pipeline;
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {"/definitely-missing-btrfsbackup-producer"},
            .consumer_argv = {"cat"},
        },
        sink,
        cancellation
    );

    test_helpers::expect_true("missing producer not started", !result.producer.started, "producer should not start");
    test_helpers::expect_true("missing producer consumer started", result.consumer.started, "consumer should start");
    test_helpers::expect_contains("missing producer diagnostics", result.producer.diagnostics, "posix_spawnp failed");
    test_helpers::expect_eq(
        "missing producer error code",
        btrfsbackup::transfer_failure_error_code(result),
        "transfer.producer_failed"
    );
}

void test_posix_pipeline_reports_consumer_failure() {
    btrfsbackup::PosixTransferPipeline pipeline;
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {"printf", "hello"},
            .consumer_argv = {"sh", "-c", "cat >/dev/null; echo consumer-error >&2; exit 9"},
        },
        sink,
        cancellation
    );

    test_helpers::expect_eq("producer exit for consumer failure", std::to_string(result.producer.exit_code), "0");
    test_helpers::expect_eq("consumer exit", std::to_string(result.consumer.exit_code), "9");
    test_helpers::expect_contains("consumer diagnostics", result.consumer.diagnostics, "consumer-error");
    test_helpers::expect_validation_error("consumer failure result", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "consumer failed with exit code 9");
}

void test_posix_pipeline_reports_missing_consumer() {
    btrfsbackup::PosixTransferPipeline pipeline;
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {"printf", "hello"},
            .consumer_argv = {"/definitely-missing-btrfsbackup-consumer"},
        },
        sink,
        cancellation
    );

    test_helpers::expect_true("missing consumer producer started", result.producer.started, "producer should start");
    test_helpers::expect_true("missing consumer not started", !result.consumer.started, "consumer should not start");
    test_helpers::expect_contains("missing consumer diagnostics", result.consumer.diagnostics, "posix_spawnp failed");
    test_helpers::expect_true(
        "missing consumer transfer failed",
        !btrfsbackup::transfer_succeeded(result),
        "missing consumer must fail the transfer"
    );
}

void test_posix_pipeline_handles_early_consumer_exit() {
    btrfsbackup::PosixTransferPipeline pipeline;
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {"yes"},
            .consumer_argv = {"sh", "-c", "echo closed >&2; exit 9"},
        },
        sink,
        cancellation
    );

    test_helpers::expect_eq("early consumer exit", std::to_string(result.consumer.exit_code), "9");
    test_helpers::expect_contains("early consumer diagnostics", result.consumer.diagnostics, "closed");
    test_helpers::expect_validation_error("early consumer failure result", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "consumer failed with exit code 9");
}

void test_posix_pipeline_honors_cancellation() {
    btrfsbackup::PosixTransferPipeline pipeline;
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;
    cancellation.request_cancel();

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {"yes"},
            .consumer_argv = {"cat"},
        },
        sink,
        cancellation
    );

    test_helpers::expect_true("posix cancelled", result.cancelled, "pipeline should report cancellation");
    test_helpers::expect_validation_error("cancelled pipeline result", [&] {
        btrfsbackup::require_transfer_success(result);
    }, "Transfer was cancelled");
}

void test_posix_pipeline_cancellation_wakes_event_loop() {
    btrfsbackup::PosixTransferPipeline pipeline;
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;

    auto started_at = std::chrono::steady_clock::now();
    std::future<btrfsbackup::TransferResult> future = std::async(
        std::launch::async,
        [&] {
            return pipeline.run(
                {
                    .producer_argv = {"sh", "-c", "sleep 5; printf late"},
                    .consumer_argv = {"cat"},
                },
                sink,
                cancellation
            );
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancellation.request_cancel();
    btrfsbackup::TransferResult result = future.get();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at
    ).count();

    test_helpers::expect_true("event loop cancellation", result.cancelled, "pipeline should cancel after start");
    test_helpers::expect_true("event loop cancellation latency", elapsed_ms < 2000, "cancellation should not wait for producer sleep");
    test_helpers::expect_true(
        "event loop cancelled event",
        std::any_of(sink.events.begin(), sink.events.end(), [](const btrfsbackup::TransferEvent& event) {
            return event.kind == btrfsbackup::TransferEventKind::Cancelled;
        }),
        "cancelled event missing"
    );
}

void test_threaded_async_pipeline_runs_in_background() {
    BlockingTransferPipeline blocking;
    btrfsbackup::ThreadedAsyncTransferPipeline async(blocking);
    RecordingEventSink sink;

    std::unique_ptr<btrfsbackup::IAsyncTransferHandle> handle = async.start(
        {
            .producer_argv = {"producer"},
            .consumer_argv = {"consumer"},
        },
        sink
    );

    wait_until_entered(blocking);
    test_helpers::expect_true("async not finished", !handle->finished(), "async handle should not finish before release");
    pollfd completion{
        .fd = handle->completion_fd(),
        .events = POLLIN | POLLHUP,
        .revents = 0,
    };
    test_helpers::expect_eq("async completion initially quiet", std::to_string(poll(&completion, 1, 0)), "0");
    blocking.allow_finish.store(true);
    test_helpers::expect_eq("async completion signalled", std::to_string(poll(&completion, 1, 1000)), "1");
    btrfsbackup::TransferResult result = handle->wait();
    test_helpers::expect_true("async transfer success", btrfsbackup::transfer_succeeded(result), "async transfer should succeed");
    test_helpers::expect_true("async finished", handle->finished(), "async handle should report completion");
}

void test_threaded_posix_pipeline_spawns_commands_in_worker_thread() {
    btrfsbackup::PosixTransferPipeline pipeline;
    btrfsbackup::ThreadedAsyncTransferPipeline async(pipeline);
    RecordingEventSink sink;

    std::unique_ptr<btrfsbackup::IAsyncTransferHandle> handle = async.start(
        {
            .producer_argv = {"printf", "threaded"},
            .consumer_argv = {"cat"},
        },
        sink
    );

    btrfsbackup::TransferResult result = handle->wait();
    test_helpers::expect_true(
        "threaded posix transfer success",
        btrfsbackup::transfer_succeeded(result),
        "worker-thread process spawn should succeed"
    );
    test_helpers::expect_eq("threaded posix bytes", std::to_string(result.bytes_transferred), "8");
}

void test_threaded_async_pipeline_requests_cancellation() {
    BlockingTransferPipeline blocking;
    btrfsbackup::ThreadedAsyncTransferPipeline async(blocking);
    RecordingEventSink sink;

    std::unique_ptr<btrfsbackup::IAsyncTransferHandle> handle = async.start(
        {
            .producer_argv = {"producer"},
            .consumer_argv = {"consumer"},
        },
        sink
    );

    wait_until_entered(blocking);
    handle->request_cancel();
    btrfsbackup::TransferResult result = handle->wait();
    test_helpers::expect_true("async cancelled", result.cancelled, "async cancellation should reach pipeline");
    test_helpers::expect_eq("async cancel error code", btrfsbackup::transfer_failure_error_code(result), "runner.cancelled");
}

} // namespace

int main() {
    test_cancellation_token();
    test_cancellation_token_signals_fd();
    test_success_result();
    test_producer_failure_is_reported_separately();
    test_consumer_failure_is_reported_separately();
    test_both_sides_failure_keeps_both_diagnostics();
    test_cancelled_transfer_is_reported();
    test_event_sink_contract();
    test_posix_pipeline_transfers_bytes();
    test_posix_pipeline_reports_producer_failure();
    test_posix_pipeline_reports_missing_producer();
    test_posix_pipeline_reports_consumer_failure();
    test_posix_pipeline_reports_missing_consumer();
    test_posix_pipeline_handles_early_consumer_exit();
    test_posix_pipeline_honors_cancellation();
    test_posix_pipeline_cancellation_wakes_event_loop();
    test_threaded_async_pipeline_runs_in_background();
    test_threaded_posix_pipeline_spawns_commands_in_worker_thread();
    test_threaded_async_pipeline_requests_cancellation();

    return test_helpers::finish("transfer pipeline tests");
}
