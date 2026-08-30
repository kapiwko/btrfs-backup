// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/transfer/PosixTransferRuntime.hpp>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <core/Errors.hpp>
#include <platform/linux/transfer/PosixTransferProcess.hpp>
#include <platform/linux/transfer/PosixTransferPump.hpp>

namespace btrfsbackup::platform::linux::transfer {

namespace {

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
    for (const PollInterest& interest : interests) {
        descriptors.push_back(interest.descriptor);
    }
    const int result = poll(descriptors.data(), descriptors.size(), timeout_ms);
    for (std::size_t index = 0; index < interests.size(); ++index) {
        interests[index].descriptor.revents = descriptors[index].revents;
    }
    return result;
}

std::pair<OwnedFileDescriptor, OwnedFileDescriptor> create_pipe() {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        throw ValidationError(std::string("cannot create transfer pipe: ") + std::strerror(errno));
    }
    return {OwnedFileDescriptor(fds[0]), OwnedFileDescriptor(fds[1])};
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
        if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
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

PosixTransferRuntime::PosixTransferRuntime(
    const btrfsbackup::backup::transfer::TransferPipelinePlan& plan,
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    CancellationToken& cancellation,
    TransferTerminationPolicy termination_policy
)
    : plan_(plan),
      events_(events),
      cancellation_(cancellation),
      termination_policy_(termination_policy),
      started_at_(TransferSteadyClock::now()),
      progress_reporter_(started_at_),
      cancellation_signal_(cancellation) {
}

void PosixTransferRuntime::initialize_transfer() {
    auto data_pipe = create_pipe();
    data_read_end_ = std::move(data_pipe.first);
    data_write_end_ = std::move(data_pipe.second);
    auto consumer_input_pipe = create_pipe();
    consumer_input_read_end_ = std::move(consumer_input_pipe.first);
    consumer_input_write_end_ = std::move(consumer_input_pipe.second);
    auto producer_error_pipe = create_pipe();
    producer_error_read_end_ = std::move(producer_error_pipe.first);
    producer_error_write_end_ = std::move(producer_error_pipe.second);
    auto consumer_error_pipe = create_pipe();
    consumer_error_read_end_ = std::move(consumer_error_pipe.first);
    consumer_error_write_end_ = std::move(consumer_error_pipe.second);
    dev_null_ = open_dev_null();
}

void PosixTransferRuntime::spawn_sides() {
    producer_spawn_ = spawn_posix_transfer_process(
        plan_.producer_argv,
        dev_null_.get(),
        data_write_end_.get(),
        producer_error_write_end_.get(),
        plan_.retained_resources,
        process::ProcessEnvironment::for_btrfs_send()
    );
    producer_process_ = process::ChildProcess(
        producer_spawn_.started() ? producer_spawn_.pid : -1,
        true,
        {
            .terminate_grace_period = termination_policy_.terminate_grace_period,
            .kill_reap_period = termination_policy_.kill_reap_period,
        }
    );
    consumer_spawn_ = spawn_posix_transfer_process(
        plan_.consumer_argv,
        consumer_input_read_end_.get(),
        dev_null_.get(),
        consumer_error_write_end_.get(),
        plan_.retained_resources,
        process::ProcessEnvironment::for_btrfs_receive()
    );
    consumer_process_ = process::ChildProcess(
        consumer_spawn_.started() ? consumer_spawn_.pid : -1,
        true,
        {
            .terminate_grace_period = termination_policy_.terminate_grace_period,
            .kill_reap_period = termination_policy_.kill_reap_period,
        }
    );

    result_.producer = producer_spawn_.started()
        ? btrfsbackup::backup::transfer::TransferSideResult::running()
        : btrfsbackup::backup::transfer::TransferSideResult::not_started(
              "posix_spawn failed for " + plan_.producer_argv.front() + ": " +
              std::strerror(producer_spawn_.error)
          );
    result_.consumer = consumer_spawn_.started()
        ? btrfsbackup::backup::transfer::TransferSideResult::running()
        : btrfsbackup::backup::transfer::TransferSideResult::not_started(
              "posix_spawn failed for " + plan_.consumer_argv.front() + ": " +
              std::strerror(consumer_spawn_.error)
          );
    result_.bytes_total_estimated = plan_.bytes_total_estimated;
}

void PosixTransferRuntime::emit_started() {
    emit_transfer_event(
        events_,
        btrfsbackup::backup::transfer::TransferEventKind::Started,
        result_,
        started_at_
    );
}

void PosixTransferRuntime::close_child_endpoints() {
    data_write_end_.reset();
    consumer_input_read_end_.reset();
    producer_error_write_end_.reset();
    consumer_error_write_end_.reset();

    if (result_.producer.started()) {
        set_nonblocking(data_read_end_.get());
        set_nonblocking(producer_error_read_end_.get());
    } else {
        data_read_end_.reset();
        producer_error_read_end_.reset();
    }
    if (result_.consumer.started()) {
        set_nonblocking(consumer_input_write_end_.get());
        set_nonblocking(consumer_error_read_end_.get());
    } else {
        consumer_input_write_end_.reset();
        consumer_error_read_end_.reset();
    }

    producer_stdout_open_ = result_.producer.started();
    consumer_stdin_open_ = result_.consumer.started();
    producer_stderr_open_ = result_.producer.started();
    consumer_stderr_open_ = result_.consumer.started();
    producer_done_ = !result_.producer.started();
    consumer_done_ = !result_.consumer.started();
    producer_termination_.emplace(
        producer_process_,
        termination_policy_.terminate_grace_period,
        termination_policy_.kill_reap_period
    );
    consumer_termination_.emplace(
        consumer_process_,
        termination_policy_.terminate_grace_period,
        termination_policy_.kill_reap_period
    );
    if (!producer_stdout_open_ && consumer_stdin_open_) {
        consumer_input_write_end_.reset();
        consumer_stdin_open_ = false;
    }
    if (!consumer_stdin_open_ && producer_stdout_open_) {
        data_read_end_.reset();
        producer_stdout_open_ = false;
        producer_termination_->request(producer_done_);
    }
}

bool PosixTransferRuntime::loop_pending() const {
    return !producer_done_ || !consumer_done_ || producer_stdout_open_ || producer_stderr_open_ ||
        consumer_stderr_open_ || producer_termination_->pending() || consumer_termination_->pending();
}

void PosixTransferRuntime::cancel_transfer() {
    if (cancellation_sent_) {
        return;
    }
    result_.cancelled = true;
    producer_termination_->request(producer_done_);
    consumer_termination_->request(consumer_done_);
    if (consumer_stdin_open_) {
        consumer_input_write_end_.reset();
        consumer_stdin_open_ = false;
    }
    if (producer_stdout_open_) {
        data_read_end_.reset();
        producer_stdout_open_ = false;
    }
    emit_transfer_event(
        events_,
        btrfsbackup::backup::transfer::TransferEventKind::Cancelled,
        result_,
        started_at_
    );
    cancellation_sent_ = true;
}

void PosixTransferRuntime::advance_terminations() {
    ChildTerminationProgress producer_progress = producer_termination_->advance(producer_done_, result_.producer);
    if (producer_progress == ChildTerminationProgress::Reaped) {
        emit_transfer_event(
            events_,
            btrfsbackup::backup::transfer::TransferEventKind::ProducerFinished,
            result_,
            started_at_
        );
    }
    if (producer_progress != ChildTerminationProgress::None) {
        producer_error_read_end_.reset();
        producer_stderr_open_ = false;
    }
    ChildTerminationProgress consumer_progress = consumer_termination_->advance(consumer_done_, result_.consumer);
    if (consumer_progress == ChildTerminationProgress::Reaped) {
        emit_transfer_event(
            events_,
            btrfsbackup::backup::transfer::TransferEventKind::ConsumerFinished,
            result_,
            started_at_
        );
    }
    if (consumer_progress != ChildTerminationProgress::None) {
        consumer_error_read_end_.reset();
        consumer_stderr_open_ = false;
    }
}

void PosixTransferRuntime::poll_once() {
    std::vector<PollInterest> interests;
    if (producer_stdout_open_ && !producer_stdout_ready_) {
        interests.push_back({
            .descriptor = {.fd = data_read_end_.get(), .events = POLLIN | POLLHUP | POLLERR, .revents = 0},
            .channel = PollChannel::ProducerOutput,
        });
    }
    if (producer_stdout_open_ && consumer_stdin_open_ && !consumer_stdin_ready_) {
        interests.push_back({
            .descriptor = {
                .fd = consumer_input_write_end_.get(),
                .events = POLLOUT | POLLHUP | POLLERR,
                .revents = 0,
            },
            .channel = PollChannel::ConsumerInput,
        });
    }
    if (producer_stderr_open_) {
        interests.push_back({
            .descriptor = {.fd = producer_error_read_end_.get(), .events = POLLIN | POLLHUP, .revents = 0},
            .channel = PollChannel::ProducerDiagnostics,
        });
    }
    if (consumer_stderr_open_) {
        interests.push_back({
            .descriptor = {.fd = consumer_error_read_end_.get(), .events = POLLIN | POLLHUP, .revents = 0},
            .channel = PollChannel::ConsumerDiagnostics,
        });
    }
    if (!cancellation_sent_) {
        interests.push_back({
            .descriptor = {.fd = cancellation_signal_.fd(), .events = POLLIN, .revents = 0},
            .channel = PollChannel::Cancellation,
        });
    }

    if (!interests.empty()) {
        const int ready = poll_interests(interests, 100);
        if (ready < 0 && errno != EINTR) {
            const std::string message = std::string("transfer poll failed: ") + std::strerror(errno);
            append_diagnostic(result_.producer.diagnostics(), message);
            append_diagnostic(result_.consumer.diagnostics(), message);
            data_read_end_.reset();
            producer_stdout_open_ = false;
            consumer_input_write_end_.reset();
            consumer_stdin_open_ = false;
            producer_error_read_end_.reset();
            producer_stderr_open_ = false;
            consumer_error_read_end_.reset();
            consumer_stderr_open_ = false;
            producer_termination_->request(producer_done_);
            consumer_termination_->request(consumer_done_);
            for (PollInterest& interest : interests) {
                interest.descriptor.revents = 0;
            }
        }
    }

    for (const PollInterest& interest : interests) {
        const short events_ready = interest.descriptor.revents;
        if (events_ready == 0) {
            continue;
        }
        if (interest.channel == PollChannel::ProducerOutput && (events_ready & POLLNVAL) != 0) {
            append_diagnostic(result_.producer.diagnostics(), "stdout pipe became invalid");
            data_read_end_.reset();
            producer_stdout_open_ = false;
            producer_stdout_ready_ = false;
            if (consumer_stdin_open_) {
                consumer_input_write_end_.reset();
                consumer_stdin_open_ = false;
                consumer_stdin_ready_ = false;
            }
            producer_termination_->request(producer_done_);
            consumer_termination_->request(consumer_done_);
        } else if (interest.channel == PollChannel::ProducerOutput && (events_ready & (POLLIN | POLLHUP)) != 0) {
            producer_stdout_ready_ = true;
        } else if (interest.channel == PollChannel::ConsumerInput && (events_ready & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            append_diagnostic(result_.consumer.diagnostics(), "stdin closed before transfer completed");
            consumer_input_write_end_.reset();
            consumer_stdin_open_ = false;
            consumer_stdin_ready_ = false;
            if (producer_stdout_open_) {
                data_read_end_.reset();
                producer_stdout_open_ = false;
                producer_stdout_ready_ = false;
            }
            producer_termination_->request(producer_done_);
        } else if (interest.channel == PollChannel::ConsumerInput && (events_ready & POLLOUT) != 0) {
            consumer_stdin_ready_ = true;
        } else if (interest.channel == PollChannel::ProducerDiagnostics && (events_ready & (POLLIN | POLLHUP)) != 0) {
            read_available(producer_error_read_end_.get(), producer_stderr_);
            if ((events_ready & POLLHUP) != 0) {
                producer_error_read_end_.reset();
                producer_stderr_open_ = false;
            }
        } else if (interest.channel == PollChannel::ConsumerDiagnostics && (events_ready & (POLLIN | POLLHUP)) != 0) {
            read_available(consumer_error_read_end_.get(), consumer_stderr_);
            if ((events_ready & POLLHUP) != 0) {
                consumer_error_read_end_.reset();
                consumer_stderr_open_ = false;
            }
        } else if (interest.channel == PollChannel::Cancellation && (events_ready & POLLIN) != 0) {
            cancellation_signal_.drain();
            cancel_transfer();
        }
    }
}

void PosixTransferRuntime::pump_ready_data() {
    constexpr std::size_t splice_cycle_budget_bytes = 16U * 1024U * 1024U;
    if (!producer_stdout_open_ || !consumer_stdin_open_ || !producer_stdout_ready_ ||
        !consumer_stdin_ready_) {
        return;
    }
    const PosixTransferPumpResult pump = pump_posix_transfer(
        data_read_end_.get(),
        consumer_input_write_end_.get(),
        splice_cycle_budget_bytes
    );
    result_.bytes_produced += pump.bytes_transferred;
    result_.bytes_transferred += pump.bytes_transferred;
    if (pump.end_of_stream) {
        data_read_end_.reset();
        producer_stdout_open_ = false;
        producer_stdout_ready_ = false;
        consumer_input_write_end_.reset();
        consumer_stdin_open_ = false;
        consumer_stdin_ready_ = false;
    } else if (pump.would_block) {
        producer_stdout_ready_ = false;
        consumer_stdin_ready_ = false;
    } else if (!pump.error.empty()) {
        append_diagnostic(result_.consumer.diagnostics(), pump.error);
        consumer_input_write_end_.reset();
        consumer_stdin_open_ = false;
        consumer_stdin_ready_ = false;
        data_read_end_.reset();
        producer_stdout_open_ = false;
        producer_stdout_ready_ = false;
        producer_termination_->request(producer_done_);
        consumer_termination_->request(consumer_done_);
    }
}

void PosixTransferRuntime::reap_children() {
    if (!producer_done_ && reap_posix_transfer_process(producer_spawn_.pid, result_.producer)) {
        producer_done_ = true;
        producer_process_.mark_reaped();
        emit_transfer_event(
            events_,
            btrfsbackup::backup::transfer::TransferEventKind::ProducerFinished,
            result_,
            started_at_
        );
    }
    if (!consumer_done_ && reap_posix_transfer_process(consumer_spawn_.pid, result_.consumer)) {
        consumer_done_ = true;
        consumer_process_.mark_reaped();
        emit_transfer_event(
            events_,
            btrfsbackup::backup::transfer::TransferEventKind::ConsumerFinished,
            result_,
            started_at_
        );
    }
}

void PosixTransferRuntime::run_event_loop() {
    while (loop_pending()) {
        if (cancellation_.cancellation_requested()) {
            cancel_transfer();
        }
        advance_terminations();
        poll_once();
        pump_ready_data();
        progress_reporter_.maybe_report(events_, result_);
        reap_children();
    }
}

void PosixTransferRuntime::finalize_diagnostics() {
    progress_reporter_.flush(events_, result_);
    append_diagnostic(result_.producer.diagnostics(), producer_stderr_.render());
    append_diagnostic(result_.consumer.diagnostics(), consumer_stderr_.render());
    trim_diagnostics(result_.producer.diagnostics());
    trim_diagnostics(result_.consumer.diagnostics());
    result_.duration_ms = transfer_elapsed_ms(started_at_);
    result_.average_speed_bps = transfer_average_speed_bps(result_.bytes_transferred, result_.duration_ms);
}

void PosixTransferRuntime::emit_completed() {
    emit_transfer_event(
        events_,
        btrfsbackup::backup::transfer::TransferEventKind::Completed,
        result_,
        started_at_
    );
}

btrfsbackup::backup::transfer::TransferResult PosixTransferRuntime::take_result() {
    return std::move(result_);
}

} // namespace btrfsbackup::platform::linux::transfer
