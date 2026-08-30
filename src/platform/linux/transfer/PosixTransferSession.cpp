// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/transfer/PosixTransferSession.hpp>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <core/Errors.hpp>
#include <platform/linux/transfer/BoundedDiagnosticBuffer.hpp>
#include <platform/linux/ChildProcess.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>
#include <platform/linux/transfer/PosixCancellationSignal.hpp>
#include <platform/linux/transfer/PosixTransferProcess.hpp>
#include <platform/linux/transfer/PosixTransferPump.hpp>
#include <platform/linux/transfer/ThreadSigpipeBlock.hpp>
#include <platform/linux/transfer/TransferChildTermination.hpp>
#include <platform/linux/transfer/TransferProgressReporter.hpp>

namespace btrfsbackup::platform::linux::transfer {

namespace {

struct Pipe {
    OwnedFileDescriptor read_end;
    OwnedFileDescriptor write_end;
};

enum class PollChannel {
    ProducerOutput,
    ConsumerInput,
    ProducerDiagnostics,
    ConsumerDiagnostics,
    Cancellation,
};

struct PollInterest {
    pollfd descriptor;
    PollChannel channel;
};

int poll_interests(std::vector<PollInterest>& interests, int timeout_ms) {
    std::vector<pollfd> descriptors;
    descriptors.reserve(interests.size());
    for (const PollInterest& interest : interests)
        descriptors.push_back(interest.descriptor);
    const int result = poll(descriptors.data(), descriptors.size(), timeout_ms);
    for (std::size_t index = 0; index < interests.size(); ++index)
        interests[index].descriptor.revents = descriptors[index].revents;
    return result;
}

Pipe create_pipe() {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        throw ValidationError(std::string("cannot create transfer pipe: ") + std::strerror(errno));
    }
    return Pipe{.read_end = OwnedFileDescriptor(fds[0]), .write_end = OwnedFileDescriptor(fds[1])};
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        throw ValidationError(std::string("cannot configure transfer pipe: ") + std::strerror(errno));
    }
}

OwnedFileDescriptor open_dev_null() {
    int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        throw ValidationError(std::string("cannot open /dev/null: ") + std::strerror(errno));
    }
    return OwnedFileDescriptor(fd);
}

void read_available(int fd, BoundedDiagnosticBuffer& output) {
    char buffer[4096];
    while (true) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            output.append(std::string_view(buffer, static_cast<std::size_t>(count)));
            continue;
        }
        if (count == 0) {
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        output.append(std::string("read failed: ") + std::strerror(errno));
        return;
    }
}

void trim_diagnostics(std::string& diagnostics) {
    while (!diagnostics.empty() && (diagnostics.back() == '\n' || diagnostics.back() == '\r')) {
        diagnostics.pop_back();
    }
}

void append_diagnostic(std::string& diagnostics, const std::string& message) {
    if (!diagnostics.empty()) {
        diagnostics += '\n';
    }
    diagnostics += message;
}

} // namespace

PosixTransferSession::PosixTransferSession(
    const btrfsbackup::backup::transfer::TransferPipelinePlan& plan,
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    CancellationToken& cancellation,
    TransferTerminationPolicy termination_policy
)
    : plan_(plan), events_(events), cancellation_(cancellation), termination_policy_(termination_policy) {
}

btrfsbackup::backup::transfer::TransferResult PosixTransferSession::run() {
    const btrfsbackup::backup::transfer::TransferPipelinePlan& plan = plan_;
    btrfsbackup::backup::transfer::ITransferEventSink& events = events_;
    CancellationToken& cancellation = cancellation_;
    const auto started_at = TransferSteadyClock::now();
    TransferProgressReporter progress_reporter(started_at);
    ThreadSigpipeBlock sigpipe_block;
    btrfsbackup::platform::linux::transfer::PosixCancellationSignal cancellation_signal(cancellation);
    Pipe data_pipe = create_pipe();
    Pipe consumer_input_pipe = create_pipe();
    Pipe producer_error_pipe = create_pipe();
    Pipe consumer_error_pipe = create_pipe();
    OwnedFileDescriptor dev_null = open_dev_null();

    ProcessSpawnResult producer_spawn = btrfsbackup::platform::linux::transfer::spawn_posix_transfer_process(
        plan.producer_argv,
        dev_null.get(),
        data_pipe.write_end.get(),
        producer_error_pipe.write_end.get(),
        plan.retained_resources
    );
    ChildProcess producer_process(
        producer_spawn.started() ? producer_spawn.pid : -1,
        true,
        {
            .terminate_grace_period = termination_policy_.terminate_grace_period,
            .kill_reap_period = termination_policy_.kill_reap_period,
        }
    );
    ProcessSpawnResult consumer_spawn = btrfsbackup::platform::linux::transfer::spawn_posix_transfer_process(
        plan.consumer_argv,
        consumer_input_pipe.read_end.get(),
        dev_null.get(),
        consumer_error_pipe.write_end.get(),
        plan.retained_resources
    );
    ChildProcess consumer_process(
        consumer_spawn.started() ? consumer_spawn.pid : -1,
        true,
        {
            .terminate_grace_period = termination_policy_.terminate_grace_period,
            .kill_reap_period = termination_policy_.kill_reap_period,
        }
    );

    btrfsbackup::backup::transfer::TransferResult result;
    result.producer = producer_spawn.started()
        ? btrfsbackup::backup::transfer::TransferSideResult::running()
        : btrfsbackup::backup::transfer::TransferSideResult::not_started(
              "posix_spawn failed for " + plan.producer_argv.front() + ": " + std::strerror(producer_spawn.error)
          );
    result.consumer = consumer_spawn.started()
        ? btrfsbackup::backup::transfer::TransferSideResult::running()
        : btrfsbackup::backup::transfer::TransferSideResult::not_started(
              "posix_spawn failed for " + plan.consumer_argv.front() + ": " + std::strerror(consumer_spawn.error)
          );
    result.bytes_total_estimated = plan.bytes_total_estimated;
    emit_transfer_event(events, btrfsbackup::backup::transfer::TransferEventKind::Started, result, started_at);

    data_pipe.write_end.reset();
    consumer_input_pipe.read_end.reset();
    producer_error_pipe.write_end.reset();
    consumer_error_pipe.write_end.reset();

    if (result.producer.started()) {
        set_nonblocking(data_pipe.read_end.get());
        set_nonblocking(producer_error_pipe.read_end.get());
    } else {
        data_pipe.read_end.reset();
        producer_error_pipe.read_end.reset();
    }
    if (result.consumer.started()) {
        set_nonblocking(consumer_input_pipe.write_end.get());
        set_nonblocking(consumer_error_pipe.read_end.get());
    } else {
        consumer_input_pipe.write_end.reset();
        consumer_error_pipe.read_end.reset();
    }

    bool producer_stdout_open = result.producer.started();
    bool consumer_stdin_open = result.consumer.started();
    bool producer_stdout_ready = false;
    bool consumer_stdin_ready = false;
    bool producer_stderr_open = result.producer.started();
    bool consumer_stderr_open = result.consumer.started();
    BoundedDiagnosticBuffer producer_stderr;
    BoundedDiagnosticBuffer consumer_stderr;
    bool producer_done = !result.producer.started();
    bool consumer_done = !result.consumer.started();
    const pid_t producer_pid = producer_spawn.pid;
    const pid_t consumer_pid = consumer_spawn.pid;
    TransferChildTermination producer_termination(
        producer_process,
        termination_policy_.terminate_grace_period,
        termination_policy_.kill_reap_period
    );
    TransferChildTermination consumer_termination(
        consumer_process,
        termination_policy_.terminate_grace_period,
        termination_policy_.kill_reap_period
    );
    if (!producer_stdout_open && consumer_stdin_open) {
        consumer_input_pipe.write_end.reset();
        consumer_stdin_open = false;
    }
    if (!consumer_stdin_open && producer_stdout_open) {
        data_pipe.read_end.reset();
        producer_stdout_open = false;
        producer_termination.request(producer_done);
    }
    bool cancellation_sent = false;
    auto cancel_transfer = [&] {
        if (cancellation_sent) {
            return;
        }
        result.cancelled = true;
        producer_termination.request(producer_done);
        consumer_termination.request(consumer_done);
        if (consumer_stdin_open) {
            consumer_input_pipe.write_end.reset();
            consumer_stdin_open = false;
        }
        if (producer_stdout_open) {
            data_pipe.read_end.reset();
            producer_stdout_open = false;
        }
        emit_transfer_event(events, btrfsbackup::backup::transfer::TransferEventKind::Cancelled, result, started_at);
        cancellation_sent = true;
    };

    while (!producer_done || !consumer_done || producer_stdout_open || producer_stderr_open || consumer_stderr_open || producer_termination.pending() || consumer_termination.pending()) {
        if (cancellation.cancellation_requested()) {
            cancel_transfer();
        }
        ChildTerminationProgress producer_progress = producer_termination.advance(producer_done, result.producer);
        if (producer_progress == ChildTerminationProgress::Reaped) {
            emit_transfer_event(events, btrfsbackup::backup::transfer::TransferEventKind::ProducerFinished, result, started_at);
        }
        if (producer_progress != ChildTerminationProgress::None) {
            producer_error_pipe.read_end.reset();
            producer_stderr_open = false;
        }
        ChildTerminationProgress consumer_progress = consumer_termination.advance(consumer_done, result.consumer);
        if (consumer_progress == ChildTerminationProgress::Reaped) {
            emit_transfer_event(events, btrfsbackup::backup::transfer::TransferEventKind::ConsumerFinished, result, started_at);
        }
        if (consumer_progress != ChildTerminationProgress::None) {
            consumer_error_pipe.read_end.reset();
            consumer_stderr_open = false;
        }

        std::vector<PollInterest> interests;

        if (producer_stdout_open && !producer_stdout_ready) {
            interests.push_back({
                .descriptor = {.fd = data_pipe.read_end.get(), .events = POLLIN | POLLHUP | POLLERR, .revents = 0},
                .channel = PollChannel::ProducerOutput,
            });
        }
        if (producer_stdout_open && consumer_stdin_open && !consumer_stdin_ready) {
            interests.push_back({
                .descriptor = {.fd = consumer_input_pipe.write_end.get(), .events = POLLOUT | POLLHUP | POLLERR, .revents = 0},
                .channel = PollChannel::ConsumerInput,
            });
        }
        if (producer_stderr_open) {
            interests.push_back({
                .descriptor = {.fd = producer_error_pipe.read_end.get(), .events = POLLIN | POLLHUP, .revents = 0},
                .channel = PollChannel::ProducerDiagnostics,
            });
        }
        if (consumer_stderr_open) {
            interests.push_back({
                .descriptor = {.fd = consumer_error_pipe.read_end.get(), .events = POLLIN | POLLHUP, .revents = 0},
                .channel = PollChannel::ConsumerDiagnostics,
            });
        }
        if (!cancellation_sent) {
            interests.push_back({
                .descriptor = {.fd = cancellation_signal.fd(), .events = POLLIN, .revents = 0},
                .channel = PollChannel::Cancellation,
            });
        }

        if (!interests.empty()) {
            const int ready = poll_interests(interests, 100);
            if (ready < 0 && errno != EINTR) {
                const std::string message = std::string("transfer poll failed: ") + std::strerror(errno);
                append_diagnostic(result.producer.diagnostics(), message);
                append_diagnostic(result.consumer.diagnostics(), message);
                data_pipe.read_end.reset();
                producer_stdout_open = false;
                consumer_input_pipe.write_end.reset();
                consumer_stdin_open = false;
                producer_error_pipe.read_end.reset();
                producer_stderr_open = false;
                consumer_error_pipe.read_end.reset();
                consumer_stderr_open = false;
                producer_termination.request(producer_done);
                consumer_termination.request(consumer_done);
                for (PollInterest& interest : interests)
                    interest.descriptor.revents = 0;
            }
        }

        for (const PollInterest& interest : interests) {
            const short events_ready = interest.descriptor.revents;
            if (events_ready == 0) {
                continue;
            }
            if (interest.channel == PollChannel::ProducerOutput && (events_ready & POLLNVAL) != 0) {
                append_diagnostic(result.producer.diagnostics(), "stdout pipe became invalid");
                data_pipe.read_end.reset();
                producer_stdout_open = false;
                producer_stdout_ready = false;
                if (consumer_stdin_open) {
                    consumer_input_pipe.write_end.reset();
                    consumer_stdin_open = false;
                    consumer_stdin_ready = false;
                }
                producer_termination.request(producer_done);
                consumer_termination.request(consumer_done);
            } else if (interest.channel == PollChannel::ProducerOutput && (events_ready & (POLLIN | POLLHUP)) != 0) {
                producer_stdout_ready = true;
            } else if (interest.channel == PollChannel::ConsumerInput && (events_ready & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
                append_diagnostic(result.consumer.diagnostics(), "stdin closed before transfer completed");
                consumer_input_pipe.write_end.reset();
                consumer_stdin_open = false;
                consumer_stdin_ready = false;
                if (producer_stdout_open) {
                    data_pipe.read_end.reset();
                    producer_stdout_open = false;
                    producer_stdout_ready = false;
                }
                producer_termination.request(producer_done);
            } else if (interest.channel == PollChannel::ConsumerInput && (events_ready & POLLOUT) != 0) {
                consumer_stdin_ready = true;
            } else if (interest.channel == PollChannel::ProducerDiagnostics && (events_ready & (POLLIN | POLLHUP)) != 0) {
                read_available(producer_error_pipe.read_end.get(), producer_stderr);
                if ((events_ready & POLLHUP) != 0) {
                    producer_error_pipe.read_end.reset();
                    producer_stderr_open = false;
                }
            } else if (interest.channel == PollChannel::ConsumerDiagnostics && (events_ready & (POLLIN | POLLHUP)) != 0) {
                read_available(consumer_error_pipe.read_end.get(), consumer_stderr);
                if ((events_ready & POLLHUP) != 0) {
                    consumer_error_pipe.read_end.reset();
                    consumer_stderr_open = false;
                }
            } else if (interest.channel == PollChannel::Cancellation && (events_ready & POLLIN) != 0) {
                cancellation_signal.drain();
                cancel_transfer();
            }
        }

        constexpr std::size_t splice_cycle_budget_bytes = 16U * 1024U * 1024U;
        if (producer_stdout_open && consumer_stdin_open && producer_stdout_ready && consumer_stdin_ready) {
            const btrfsbackup::platform::linux::transfer::PosixTransferPumpResult pump = btrfsbackup::platform::linux::transfer::pump_posix_transfer(
                data_pipe.read_end.get(),
                consumer_input_pipe.write_end.get(),
                splice_cycle_budget_bytes
            );
            result.bytes_produced += pump.bytes_transferred;
            result.bytes_transferred += pump.bytes_transferred;
            if (pump.end_of_stream) {
                data_pipe.read_end.reset();
                producer_stdout_open = false;
                producer_stdout_ready = false;
                consumer_input_pipe.write_end.reset();
                consumer_stdin_open = false;
                consumer_stdin_ready = false;
            } else if (pump.would_block) {
                producer_stdout_ready = false;
                consumer_stdin_ready = false;
            } else if (!pump.error.empty()) {
                append_diagnostic(result.consumer.diagnostics(), pump.error);
                consumer_input_pipe.write_end.reset();
                consumer_stdin_open = false;
                consumer_stdin_ready = false;
                data_pipe.read_end.reset();
                producer_stdout_open = false;
                producer_stdout_ready = false;
                producer_termination.request(producer_done);
                consumer_termination.request(consumer_done);
            }
        }

        progress_reporter.maybe_report(events, result);

        if (!producer_done && btrfsbackup::platform::linux::transfer::reap_posix_transfer_process(producer_pid, result.producer)) {
            producer_done = true;
            producer_process.mark_reaped();
            emit_transfer_event(events, btrfsbackup::backup::transfer::TransferEventKind::ProducerFinished, result, started_at);
        }
        if (!consumer_done && btrfsbackup::platform::linux::transfer::reap_posix_transfer_process(consumer_pid, result.consumer)) {
            consumer_done = true;
            consumer_process.mark_reaped();
            emit_transfer_event(events, btrfsbackup::backup::transfer::TransferEventKind::ConsumerFinished, result, started_at);
        }
    }

    progress_reporter.flush(events, result);
    append_diagnostic(result.producer.diagnostics(), producer_stderr.render());
    append_diagnostic(result.consumer.diagnostics(), consumer_stderr.render());
    trim_diagnostics(result.producer.diagnostics());
    trim_diagnostics(result.consumer.diagnostics());
    result.duration_ms = transfer_elapsed_ms(started_at);
    result.average_speed_bps = transfer_average_speed_bps(result.bytes_transferred, result.duration_ms);
    emit_transfer_event(events, btrfsbackup::backup::transfer::TransferEventKind::Completed, result, started_at);
    return result;
}

} // namespace btrfsbackup::platform::linux::transfer
