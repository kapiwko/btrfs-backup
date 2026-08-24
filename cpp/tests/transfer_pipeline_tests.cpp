#include <poll.h>
#include <sys/wait.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>
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

class ThrowingStartedEventSink final : public btrfsbackup::ITransferEventSink {
public:
    void on_transfer_event(const btrfsbackup::TransferEvent& event) override {
        if (event.kind == btrfsbackup::TransferEventKind::Started) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            throw std::runtime_error("injected event sink failure");
        }
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

void test_transfer_speed_uses_recent_samples() {
    btrfsbackup::TransferSpeedEstimator speed;
    const std::uint64_t one_mib = 1024ULL * 1024ULL;

    test_helpers::expect_eq("initial speed", std::to_string(speed.sample(one_mib, 1000)), std::to_string(one_mib));
    const std::uint64_t accelerated = speed.sample(5 * one_mib, 2000);
    test_helpers::expect_true(
        "EWMA responds to acceleration",
        accelerated > one_mib && accelerated < 4 * one_mib,
        "smoothed speed should move toward the recent sample without jumping to it"
    );
    const std::uint64_t after_stall = speed.sample(5 * one_mib, 3000);
    test_helpers::expect_true("EWMA decays after stall", after_stall < accelerated, "smoothed speed should decay without new bytes");
}

void test_posix_pipeline_validates_termination_policy() {
    test_helpers::expect_validation_error("zero terminate period", [] {
        btrfsbackup::PosixTransferPipeline pipeline({
            .terminate_grace_period = std::chrono::milliseconds(0),
            .kill_reap_period = std::chrono::milliseconds(100),
        });
    }, "termination periods must be positive");
    test_helpers::expect_validation_error("zero kill reap period", [] {
        btrfsbackup::PosixTransferPipeline pipeline({
            .terminate_grace_period = std::chrono::milliseconds(100),
            .kill_reap_period = std::chrono::milliseconds(0),
        });
    }, "termination periods must be positive");
}

void test_posix_pipeline_reaps_children_when_setup_unwinds() {
    const std::filesystem::path root = test_helpers::test_root("transfer-pipeline", "setup-unwind");
    const std::filesystem::path producer_pid_path = root / "producer.pid";
    const std::filesystem::path consumer_pid_path = root / "consumer.pid";
    btrfsbackup::PosixTransferPipeline pipeline({
        .terminate_grace_period = std::chrono::milliseconds(50),
        .kill_reap_period = std::chrono::milliseconds(500),
    });
    ThrowingStartedEventSink sink;
    btrfsbackup::CancellationToken cancellation;
    auto started_at = std::chrono::steady_clock::now();

    try {
        pipeline.run(
            {
                .producer_argv = {"sh", "-c", "trap '' TERM; printf %s $$ >\"$1\"; while :; do sleep 1; done", "producer", producer_pid_path.string()},
                .consumer_argv = {"sh", "-c", "trap '' TERM; printf %s $$ >\"$1\"; while :; do sleep 1; done", "consumer", consumer_pid_path.string()},
            },
            sink,
            cancellation
        );
        test_helpers::fail("pipeline setup unwind", "expected event sink failure");
    } catch (const std::runtime_error&) {
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at
    ).count();
    test_helpers::expect_true("pipeline unwind bounded", elapsed_ms < 2000, "process cleanup exceeded its deadline");
    for (const std::filesystem::path& pid_path : {producer_pid_path, consumer_pid_path}) {
        std::ifstream pid_file(pid_path);
        pid_t pid = -1;
        pid_file >> pid;
        test_helpers::expect_true("pipeline child pid recorded", pid > 0, "child did not record its pid");
        int status = 0;
        errno = 0;
        test_helpers::expect_eq("pipeline child already reaped", std::to_string(waitpid(pid, &status, WNOHANG)), "-1");
        test_helpers::expect_eq("pipeline child wait status", std::to_string(errno), std::to_string(ECHILD));
    }
    std::filesystem::remove_all(root);
}

void test_posix_pipeline_preserves_stream_integrity() {
    constexpr std::uint64_t transfer_bytes = 8ULL * 1024ULL * 1024ULL;
    btrfsbackup::PosixTransferPipeline pipeline;
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;
    const std::filesystem::path root = test_helpers::test_root("transfer-pipeline", "splice-integrity");
    const std::filesystem::path input_path = root / "input.bin";
    std::string chunk(64U * 1024U, '\0');
    for (std::size_t index = 0; index < chunk.size(); ++index) {
        chunk[index] = static_cast<char>(index % 251U);
    }
    {
        std::ofstream input(input_path, std::ios::binary);
        for (std::uint64_t written = 0; written < transfer_bytes; written += chunk.size()) {
            input.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
    }

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {"cat", input_path.string()},
            .consumer_argv = {"sh", "-c", "sleep 0.1; cmp - \"$1\"", "slow-cmp", input_path.string()},
        },
        sink,
        cancellation
    );

    test_helpers::expect_true("splice integrity transfer success", btrfsbackup::transfer_succeeded(result), "splice transfer should succeed");
    test_helpers::expect_eq("splice integrity transfer bytes", std::to_string(result.bytes_transferred), std::to_string(transfer_bytes));
    const auto progress_events = std::count_if(sink.events.begin(), sink.events.end(), [](const btrfsbackup::TransferEvent& event) {
        return event.kind == btrfsbackup::TransferEventKind::Progress;
    });
    test_helpers::expect_true("progress events throttled", progress_events < 100, "progress should not be emitted for every splice chunk");
    std::filesystem::remove_all(root);
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

void test_posix_pipeline_transfers_gibibyte_under_backpressure() {
    btrfsbackup::PosixTransferPipeline pipeline;
    btrfsbackup::NullTransferEventSink sink;
    btrfsbackup::CancellationToken cancellation;
    constexpr std::uint64_t transfer_bytes = 1024ULL * 1024ULL * 1024ULL;

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {"head", "-c", std::to_string(transfer_bytes), "/dev/zero"},
            .consumer_argv = {"sh", "-c", "sleep 0.2; cat >/dev/null"},
        },
        sink,
        cancellation
    );

    test_helpers::expect_true("backpressured transfer success", btrfsbackup::transfer_succeeded(result), "large transfer should succeed");
    test_helpers::expect_eq("backpressured transfer bytes", std::to_string(result.bytes_transferred), std::to_string(transfer_bytes));
    test_helpers::expect_eq("backpressured produced bytes", std::to_string(result.bytes_produced), std::to_string(transfer_bytes));
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
    test_helpers::expect_contains("missing producer diagnostics", result.producer.diagnostics, "posix_spawn failed");
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
    test_helpers::expect_contains("missing consumer diagnostics", result.consumer.diagnostics, "posix_spawn failed");
    test_helpers::expect_true(
        "missing consumer transfer failed",
        !btrfsbackup::transfer_succeeded(result),
        "missing consumer must fail the transfer"
    );
}

void test_posix_pipeline_reaps_live_producer_when_consumer_spawn_fails() {
    btrfsbackup::PosixTransferPipeline pipeline({
        .terminate_grace_period = std::chrono::milliseconds(100),
        .kill_reap_period = std::chrono::milliseconds(500),
    });
    RecordingEventSink sink;
    btrfsbackup::CancellationToken cancellation;
    auto started_at = std::chrono::steady_clock::now();

    btrfsbackup::TransferResult result = pipeline.run(
        {
            .producer_argv = {
                "/bin/sh",
                "-c",
                "trap '' TERM; while :; do sleep 1; done",
            },
            .consumer_argv = {"definitely-missing-btrfsbackup-consumer"},
        },
        sink,
        cancellation
    );

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at
    ).count();
    test_helpers::expect_true("partial spawn producer started", result.producer.started, "producer should start before consumer failure");
    test_helpers::expect_true("partial spawn consumer missing", !result.consumer.started, "consumer should fail to start");
    test_helpers::expect_true("partial spawn cleanup bounded", elapsed_ms < 2000, "producer cleanup exceeded its deadline");
    test_helpers::expect_contains("partial spawn consumer diagnostics", result.consumer.diagnostics, "posix_spawn failed");
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

void test_posix_pipeline_cancels_while_backpressured() {
    btrfsbackup::PosixTransferPipeline pipeline;
    btrfsbackup::NullTransferEventSink sink;
    btrfsbackup::CancellationToken cancellation;

    auto started_at = std::chrono::steady_clock::now();
    std::future<btrfsbackup::TransferResult> future = std::async(
        std::launch::async,
        [&] {
            return pipeline.run(
                {
                    .producer_argv = {"yes"},
                    .consumer_argv = {"sh", "-c", "sleep 5; cat >/dev/null"},
                },
                sink,
                cancellation
            );
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cancellation.request_cancel();
    btrfsbackup::TransferResult result = future.get();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at
    ).count();

    test_helpers::expect_true("backpressured cancellation", result.cancelled, "backpressured transfer should cancel");
    test_helpers::expect_true("backpressured cancellation latency", elapsed_ms < 2000, "cancellation should not wait for the consumer");
}

void test_posix_pipeline_kills_children_that_ignore_sigterm() {
    btrfsbackup::PosixTransferPipeline pipeline({
        .terminate_grace_period = std::chrono::milliseconds(100),
        .kill_reap_period = std::chrono::milliseconds(500),
    });
    btrfsbackup::NullTransferEventSink sink;
    btrfsbackup::CancellationToken cancellation;

    auto started_at = std::chrono::steady_clock::now();
    std::future<btrfsbackup::TransferResult> future = std::async(
        std::launch::async,
        [&] {
            return pipeline.run(
                {
                    .producer_argv = {"sh", "-c", "trap '' TERM; while :; do sleep 1; done"},
                    .consumer_argv = {"sh", "-c", "trap '' TERM; while :; do sleep 1; done"},
                },
                sink,
                cancellation
            );
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cancellation.request_cancel();
    btrfsbackup::TransferResult result = future.get();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at
    ).count();

    test_helpers::expect_true("stubborn transfer cancelled", result.cancelled, "transfer should report cancellation");
    test_helpers::expect_true("stubborn cancellation bounded", elapsed_ms < 2000, "SIGKILL escalation should bound cancellation");
    test_helpers::expect_eq("stubborn producer killed", std::to_string(result.producer.exit_code), "137");
    test_helpers::expect_eq("stubborn consumer killed", std::to_string(result.consumer.exit_code), "137");
    test_helpers::expect_contains("producer escalation diagnostic", result.producer.diagnostics, "sent SIGKILL");
    test_helpers::expect_contains("consumer escalation diagnostic", result.consumer.diagnostics, "sent SIGKILL");
}

void test_async_handle_destructor_kills_stubborn_children() {
    btrfsbackup::PosixTransferPipeline pipeline({
        .terminate_grace_period = std::chrono::milliseconds(100),
        .kill_reap_period = std::chrono::milliseconds(500),
    });
    btrfsbackup::ThreadedAsyncTransferPipeline async(pipeline);
    btrfsbackup::NullTransferEventSink sink;
    auto started_at = std::chrono::steady_clock::now();

    {
        std::unique_ptr<btrfsbackup::IAsyncTransferHandle> handle = async.start(
            {
                .producer_argv = {"sh", "-c", "trap '' TERM; while :; do sleep 1; done"},
                .consumer_argv = {"sh", "-c", "trap '' TERM; while :; do sleep 1; done"},
            },
            sink
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at
    ).count();
    test_helpers::expect_true("async destructor cancellation bounded", elapsed_ms < 2000, "handle destruction should not wait indefinitely");
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
    test_transfer_speed_uses_recent_samples();
    test_posix_pipeline_validates_termination_policy();
    test_posix_pipeline_reaps_children_when_setup_unwinds();
    test_posix_pipeline_preserves_stream_integrity();
    test_posix_pipeline_transfers_bytes();
    test_posix_pipeline_transfers_gibibyte_under_backpressure();
    test_posix_pipeline_reports_producer_failure();
    test_posix_pipeline_reports_missing_producer();
    test_posix_pipeline_reports_consumer_failure();
    test_posix_pipeline_reports_missing_consumer();
    test_posix_pipeline_reaps_live_producer_when_consumer_spawn_fails();
    test_posix_pipeline_handles_early_consumer_exit();
    test_posix_pipeline_honors_cancellation();
    test_posix_pipeline_cancellation_wakes_event_loop();
    test_posix_pipeline_cancels_while_backpressured();
    test_posix_pipeline_kills_children_that_ignore_sigterm();
    test_async_handle_destructor_kills_stubborn_children();
    test_threaded_async_pipeline_runs_in_background();
    test_threaded_posix_pipeline_spawns_commands_in_worker_thread();
    test_threaded_async_pipeline_requests_cancellation();

    return test_helpers::finish("transfer pipeline tests");
}
