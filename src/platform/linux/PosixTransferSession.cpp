// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/PosixTransferSession.hpp>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <core/Errors.hpp>
#include <backup/transfer/TransferSpeedEstimator.hpp>
#include <platform/linux/ChildProcess.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>
#include <platform/linux/PosixCancellationSignal.hpp>
#include <platform/linux/PosixTransferProcess.hpp>
#include <platform/linux/PosixTransferPump.hpp>

namespace btrfsbackup::platform::linux {

namespace {

class ScopedIgnoredSigpipe {
  public:
    ScopedIgnoredSigpipe() {
        struct sigaction action{};
        action.sa_handler = SIG_IGN;
        sigemptyset(&action.sa_mask);
        sigaction(SIGPIPE, &action, &previous_);
    }
    ScopedIgnoredSigpipe(const ScopedIgnoredSigpipe&) = delete;
    ScopedIgnoredSigpipe& operator=(const ScopedIgnoredSigpipe&) = delete;
    ~ScopedIgnoredSigpipe() {
        sigaction(SIGPIPE, &previous_, nullptr);
    }

  private:
    struct sigaction previous_{};
};

struct Pipe {
    OwnedFileDescriptor read_end;
    OwnedFileDescriptor write_end;
};

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

struct ChildTerminationState {
    ChildProcess* process = nullptr;
    bool terminate_sent = false;
    bool kill_sent = false;
    std::optional<std::chrono::steady_clock::time_point> deadline;
};

enum class ChildTerminationProgress {
    None,
    Reaped,
    Abandoned,
};

class BoundedDiagnosticBuffer {
  public:
    void append(std::string_view data) {
        const std::size_t head_remaining = segment_limit_bytes - head_.size();
        const std::size_t head_bytes = std::min(head_remaining, data.size());
        head_.append(data.data(), head_bytes);
        data.remove_prefix(head_bytes);
        if (data.empty()) {
            return;
        }

        if (data.size() >= segment_limit_bytes) {
            discarded_bytes_ += tail_.size() + data.size() - segment_limit_bytes;
            tail_.assign(data.substr(data.size() - segment_limit_bytes));
            tail_start_ = 0;
            return;
        }

        const std::size_t tail_remaining = segment_limit_bytes - tail_.size();
        const std::size_t appended_bytes = std::min(tail_remaining, data.size());
        tail_.append(data.data(), appended_bytes);
        data.remove_prefix(appended_bytes);
        if (data.empty()) {
            return;
        }

        discarded_bytes_ += data.size();
        const std::size_t first_part = std::min(data.size(), segment_limit_bytes - tail_start_);
        std::memcpy(tail_.data() + tail_start_, data.data(), first_part);
        std::memcpy(tail_.data(), data.data() + first_part, data.size() - first_part);
        tail_start_ = (tail_start_ + data.size()) % segment_limit_bytes;
    }

    [[nodiscard]] std::string render() const {
        if (discarded_bytes_ == 0) {
            return head_ + tail_;
        }
        return head_ + "\n... omitted " + std::to_string(discarded_bytes_) +
            " diagnostic bytes ...\n" + tail_text();
    }

  private:
    [[nodiscard]] std::string tail_text() const {
        if (tail_start_ == 0) {
            return tail_;
        }
        return tail_.substr(tail_start_) + tail_.substr(0, tail_start_);
    }

    static constexpr std::size_t segment_limit_bytes = 64U * 1024U;
    std::string head_;
    std::string tail_;
    std::size_t tail_start_ = 0;
    std::uint64_t discarded_bytes_ = 0;
};

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

using SteadyClock = std::chrono::steady_clock;

std::uint64_t elapsed_ms_since(SteadyClock::time_point started_at) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - started_at).count()
    );
}

std::uint64_t average_speed_bps(std::uint64_t bytes, std::uint64_t elapsed_ms) {
    if (elapsed_ms == 0) {
        return 0;
    }
    const long double rate = static_cast<long double>(bytes) * 1000.0L / static_cast<long double>(elapsed_ms);
    return rate >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(rate);
}

void emit_event(
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    btrfsbackup::backup::transfer::TransferEventKind kind,
    const btrfsbackup::backup::transfer::TransferResult& result,
    SteadyClock::time_point started_at,
    std::uint64_t delta_bytes = 0,
    const std::string& message = "",
    std::optional<std::uint64_t> reported_speed_bps = std::nullopt
) {
    std::uint64_t elapsed = elapsed_ms_since(started_at);
    events.on_transfer_event({
        .kind = kind,
        .bytes_transferred = result.bytes_transferred,
        .bytes_produced = result.bytes_produced,
        .bytes_total_estimated = result.bytes_total_estimated,
        .delta_bytes = delta_bytes,
        .elapsed_ms = elapsed,
        .speed_bps = reported_speed_bps.value_or(average_speed_bps(result.bytes_transferred, elapsed)),
        .message = message,
    });
}

class TransferProgressReporter {
  public:
    explicit TransferProgressReporter(SteadyClock::time_point started_at)
        : started_at_(started_at), last_report_at_(started_at) {
    }

    void maybe_report(btrfsbackup::backup::transfer::ITransferEventSink& events, const btrfsbackup::backup::transfer::TransferResult& result) {
        const SteadyClock::time_point now = SteadyClock::now();
        if (now - last_report_at_ < report_interval_) {
            return;
        }
        report(events, result, now);
    }

    void flush(btrfsbackup::backup::transfer::ITransferEventSink& events, const btrfsbackup::backup::transfer::TransferResult& result) {
        if (reported_ && result.bytes_transferred == last_reported_bytes_) {
            return;
        }
        report(events, result, SteadyClock::now());
    }

  private:
    void report(btrfsbackup::backup::transfer::ITransferEventSink& events, const btrfsbackup::backup::transfer::TransferResult& result, SteadyClock::time_point now) {
        const std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - started_at_
        );
        const std::uint64_t delta_bytes = result.bytes_transferred >= last_reported_bytes_
            ? result.bytes_transferred - last_reported_bytes_
            : 0;
        const std::uint64_t smoothed_speed = speed_.sample(result.bytes_transferred, elapsed);
        emit_event(
            events,
            btrfsbackup::backup::transfer::TransferEventKind::Progress,
            result,
            started_at_,
            delta_bytes,
            "",
            smoothed_speed
        );
        last_report_at_ = now;
        last_reported_bytes_ = result.bytes_transferred;
        reported_ = true;
    }

    static constexpr std::chrono::milliseconds report_interval_{500};
    SteadyClock::time_point started_at_;
    SteadyClock::time_point last_report_at_;
    btrfsbackup::backup::transfer::TransferSpeedEstimator speed_;
    std::uint64_t last_reported_bytes_ = 0;
    bool reported_ = false;
};

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
    const auto started_at = SteadyClock::now();
    TransferProgressReporter progress_reporter(started_at);
    ScopedIgnoredSigpipe ignored_sigpipe;
    btrfsbackup::platform::linux::PosixCancellationSignal cancellation_signal(cancellation);
    Pipe data_pipe = create_pipe();
    Pipe consumer_input_pipe = create_pipe();
    Pipe producer_error_pipe = create_pipe();
    Pipe consumer_error_pipe = create_pipe();
    OwnedFileDescriptor dev_null = open_dev_null();

    ProcessSpawnResult producer_spawn = btrfsbackup::platform::linux::spawn_posix_transfer_process(
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
    ProcessSpawnResult consumer_spawn = btrfsbackup::platform::linux::spawn_posix_transfer_process(
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
    emit_event(events, btrfsbackup::backup::transfer::TransferEventKind::Started, result, started_at);

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
    ChildTerminationState producer_termination{
        .process = &producer_process,
        .terminate_sent = false,
        .kill_sent = false,
        .deadline = std::nullopt,
    };
    ChildTerminationState consumer_termination{
        .process = &consumer_process,
        .terminate_sent = false,
        .kill_sent = false,
        .deadline = std::nullopt,
    };
    auto request_termination = [&](ChildTerminationState& state, bool done) {
        if (state.terminate_sent || state.process == nullptr || state.process->pid() <= 0 || (done && !state.process->process_group_exists())) {
            return;
        }
        state.process->send_signal(SIGTERM);
        state.terminate_sent = true;
        state.deadline = SteadyClock::now() + termination_policy_.terminate_grace_period;
    };
    auto advance_termination = [&](
                                   ChildTerminationState& state,
                                   bool& done,
                                   btrfsbackup::backup::transfer::TransferSideResult& side
                               ) {
        if (!state.terminate_sent || !state.deadline.has_value()) {
            return ChildTerminationProgress::None;
        }
        if (done && !state.process->process_group_exists()) {
            state.deadline.reset();
            return ChildTerminationProgress::None;
        }
        if (SteadyClock::now() < *state.deadline) {
            return ChildTerminationProgress::None;
        }
        if (!state.kill_sent) {
            state.process->send_signal(SIGKILL);
            state.kill_sent = true;
            state.deadline = SteadyClock::now() + termination_policy_.kill_reap_period;
            append_diagnostic(side.diagnostics(), "did not exit after SIGTERM; sent SIGKILL");
            return ChildTerminationProgress::None;
        }

        state.deadline.reset();
        if (done) {
            return ChildTerminationProgress::Abandoned;
        }
        if (btrfsbackup::platform::linux::reap_posix_transfer_process(state.process->pid(), side)) {
            done = true;
            state.process->mark_reaped();
            return ChildTerminationProgress::Reaped;
        }
        side.mark_exited(128 + SIGKILL);
        append_diagnostic(side.diagnostics(), "did not become waitable after SIGKILL");
        done = true;
        state.process->release();
        return ChildTerminationProgress::Abandoned;
    };
    if (!producer_stdout_open && consumer_stdin_open) {
        consumer_input_pipe.write_end.reset();
        consumer_stdin_open = false;
    }
    if (!consumer_stdin_open && producer_stdout_open) {
        data_pipe.read_end.reset();
        producer_stdout_open = false;
        request_termination(producer_termination, producer_done);
    }
    bool cancellation_sent = false;
    auto cancel_transfer = [&] {
        if (cancellation_sent) {
            return;
        }
        result.cancelled = true;
        request_termination(producer_termination, producer_done);
        request_termination(consumer_termination, consumer_done);
        if (consumer_stdin_open) {
            consumer_input_pipe.write_end.reset();
            consumer_stdin_open = false;
        }
        if (producer_stdout_open) {
            data_pipe.read_end.reset();
            producer_stdout_open = false;
        }
        emit_event(events, btrfsbackup::backup::transfer::TransferEventKind::Cancelled, result, started_at);
        cancellation_sent = true;
    };

    auto termination_pending = [](const ChildTerminationState& state) {
        return state.terminate_sent && state.deadline.has_value();
    };
    while (!producer_done || !consumer_done || producer_stdout_open || producer_stderr_open || consumer_stderr_open || termination_pending(producer_termination) || termination_pending(consumer_termination)) {
        if (cancellation.cancellation_requested()) {
            cancel_transfer();
        }
        ChildTerminationProgress producer_progress = advance_termination(
            producer_termination,
            producer_done,
            result.producer
        );
        if (producer_progress == ChildTerminationProgress::Reaped) {
            emit_event(events, btrfsbackup::backup::transfer::TransferEventKind::ProducerFinished, result, started_at);
        }
        if (producer_progress != ChildTerminationProgress::None) {
            producer_error_pipe.read_end.reset();
            producer_stderr_open = false;
        }
        ChildTerminationProgress consumer_progress = advance_termination(
            consumer_termination,
            consumer_done,
            result.consumer
        );
        if (consumer_progress == ChildTerminationProgress::Reaped) {
            emit_event(events, btrfsbackup::backup::transfer::TransferEventKind::ConsumerFinished, result, started_at);
        }
        if (consumer_progress != ChildTerminationProgress::None) {
            consumer_error_pipe.read_end.reset();
            consumer_stderr_open = false;
        }

        std::vector<pollfd> fds;
        std::vector<int> tags;
        constexpr int producer_stdout_tag = 1;
        constexpr int consumer_stdin_tag = 2;
        constexpr int producer_stderr_tag = 3;
        constexpr int consumer_stderr_tag = 4;
        constexpr int cancellation_tag = 5;

        if (producer_stdout_open && !producer_stdout_ready) {
            fds.push_back({.fd = data_pipe.read_end.get(), .events = POLLIN | POLLHUP | POLLERR, .revents = 0});
            tags.push_back(producer_stdout_tag);
        }
        if (producer_stdout_open && consumer_stdin_open && !consumer_stdin_ready) {
            fds.push_back({.fd = consumer_input_pipe.write_end.get(), .events = POLLOUT | POLLHUP | POLLERR, .revents = 0});
            tags.push_back(consumer_stdin_tag);
        }
        if (producer_stderr_open) {
            fds.push_back({.fd = producer_error_pipe.read_end.get(), .events = POLLIN | POLLHUP, .revents = 0});
            tags.push_back(producer_stderr_tag);
        }
        if (consumer_stderr_open) {
            fds.push_back({.fd = consumer_error_pipe.read_end.get(), .events = POLLIN | POLLHUP, .revents = 0});
            tags.push_back(consumer_stderr_tag);
        }
        if (!cancellation_sent) {
            fds.push_back({.fd = cancellation_signal.fd(), .events = POLLIN, .revents = 0});
            tags.push_back(cancellation_tag);
        }

        if (!fds.empty()) {
            int ready = poll(fds.data(), fds.size(), 100);
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
                request_termination(producer_termination, producer_done);
                request_termination(consumer_termination, consumer_done);
                for (pollfd& fd : fds) {
                    fd.revents = 0;
                }
            }
        }

        for (std::size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].revents == 0) {
                continue;
            }
            int tag = tags[i];
            if (tag == producer_stdout_tag && (fds[i].revents & POLLNVAL) != 0) {
                append_diagnostic(result.producer.diagnostics(), "stdout pipe became invalid");
                data_pipe.read_end.reset();
                producer_stdout_open = false;
                producer_stdout_ready = false;
                if (consumer_stdin_open) {
                    consumer_input_pipe.write_end.reset();
                    consumer_stdin_open = false;
                    consumer_stdin_ready = false;
                }
                request_termination(producer_termination, producer_done);
                request_termination(consumer_termination, consumer_done);
            } else if (tag == producer_stdout_tag && (fds[i].revents & (POLLIN | POLLHUP)) != 0) {
                producer_stdout_ready = true;
            } else if (tag == consumer_stdin_tag && (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
                append_diagnostic(result.consumer.diagnostics(), "stdin closed before transfer completed");
                consumer_input_pipe.write_end.reset();
                consumer_stdin_open = false;
                consumer_stdin_ready = false;
                if (producer_stdout_open) {
                    data_pipe.read_end.reset();
                    producer_stdout_open = false;
                    producer_stdout_ready = false;
                }
                request_termination(producer_termination, producer_done);
            } else if (tag == consumer_stdin_tag && (fds[i].revents & POLLOUT) != 0) {
                consumer_stdin_ready = true;
            } else if (tag == producer_stderr_tag && (fds[i].revents & (POLLIN | POLLHUP)) != 0) {
                read_available(producer_error_pipe.read_end.get(), producer_stderr);
                if ((fds[i].revents & POLLHUP) != 0) {
                    producer_error_pipe.read_end.reset();
                    producer_stderr_open = false;
                }
            } else if (tag == consumer_stderr_tag && (fds[i].revents & (POLLIN | POLLHUP)) != 0) {
                read_available(consumer_error_pipe.read_end.get(), consumer_stderr);
                if ((fds[i].revents & POLLHUP) != 0) {
                    consumer_error_pipe.read_end.reset();
                    consumer_stderr_open = false;
                }
            } else if (tag == cancellation_tag && (fds[i].revents & POLLIN) != 0) {
                cancellation_signal.drain();
                cancel_transfer();
            }
        }

        constexpr std::size_t splice_cycle_budget_bytes = 16U * 1024U * 1024U;
        if (producer_stdout_open && consumer_stdin_open && producer_stdout_ready && consumer_stdin_ready) {
            const btrfsbackup::platform::linux::PosixTransferPumpResult pump = btrfsbackup::platform::linux::pump_posix_transfer(
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
                request_termination(producer_termination, producer_done);
                request_termination(consumer_termination, consumer_done);
            }
        }

        progress_reporter.maybe_report(events, result);

        if (!producer_done && btrfsbackup::platform::linux::reap_posix_transfer_process(producer_pid, result.producer)) {
            producer_done = true;
            producer_process.mark_reaped();
            emit_event(events, btrfsbackup::backup::transfer::TransferEventKind::ProducerFinished, result, started_at);
        }
        if (!consumer_done && btrfsbackup::platform::linux::reap_posix_transfer_process(consumer_pid, result.consumer)) {
            consumer_done = true;
            consumer_process.mark_reaped();
            emit_event(events, btrfsbackup::backup::transfer::TransferEventKind::ConsumerFinished, result, started_at);
        }
    }

    progress_reporter.flush(events, result);
    append_diagnostic(result.producer.diagnostics(), producer_stderr.render());
    append_diagnostic(result.consumer.diagnostics(), consumer_stderr.render());
    trim_diagnostics(result.producer.diagnostics());
    trim_diagnostics(result.consumer.diagnostics());
    result.duration_ms = elapsed_ms_since(started_at);
    result.average_speed_bps = average_speed_bps(result.bytes_transferred, result.duration_ms);
    emit_event(events, btrfsbackup::backup::transfer::TransferEventKind::Completed, result, started_at);
    return result;
}

} // namespace btrfsbackup::platform::linux
