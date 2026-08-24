#include <btrfsbackup/transfer_pipeline.hpp>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/process_spawn.hpp>

namespace btrfsbackup {

void NullTransferEventSink::on_transfer_event(const TransferEvent&) {
}

CancellationToken::CancellationToken() {
    if (pipe2(cancellation_pipe_, O_CLOEXEC | O_NONBLOCK) != 0) {
        throw ValidationError(std::string("cannot create cancellation pipe: ") + std::strerror(errno));
    }
}

CancellationToken::~CancellationToken() {
    if (cancellation_pipe_[0] >= 0) {
        close(cancellation_pipe_[0]);
    }
    if (cancellation_pipe_[1] >= 0) {
        close(cancellation_pipe_[1]);
    }
}

void CancellationToken::request_cancel() {
    bool already_requested = cancellation_requested_.exchange(true);
    if (already_requested || cancellation_pipe_[1] < 0) {
        return;
    }

    char byte = 1;
    ssize_t ignored = write(cancellation_pipe_[1], &byte, sizeof(byte));
    (void)ignored;
}

bool CancellationToken::cancellation_requested() const {
    return cancellation_requested_.load();
}

int CancellationToken::cancellation_fd() const {
    return cancellation_pipe_[0];
}

void CancellationToken::drain_cancellation_signal() const {
    if (cancellation_pipe_[0] < 0) {
        return;
    }

    char buffer[32];
    while (true) {
        ssize_t count = read(cancellation_pipe_[0], buffer, sizeof(buffer));
        if (count > 0) {
            continue;
        }
        if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        return;
    }
}

bool transfer_succeeded(const TransferResult& result) {
    return !result.cancelled
        && result.producer.started
        && result.consumer.started
        && result.producer.exit_code == 0
        && result.consumer.exit_code == 0;
}

std::string transfer_failure_error_code(const TransferResult& result) {
    if (transfer_succeeded(result)) {
        return {};
    }
    if (result.cancelled) {
        return "runner.cancelled";
    }

    const bool producer_failed = !result.producer.started || result.producer.exit_code != 0;
    const bool consumer_failed = !result.consumer.started || result.consumer.exit_code != 0;
    if (producer_failed && consumer_failed) {
        return "transfer.producer_consumer_failed";
    }
    if (producer_failed) {
        return "transfer.producer_failed";
    }
    if (consumer_failed) {
        return "transfer.consumer_failed";
    }
    return "transfer.failed";
}

namespace {

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~UniqueFd() {
        reset();
    }

    int get() const {
        return fd_;
    }

    int release() {
        int result = fd_;
        fd_ = -1;
        return result;
    }

    void reset(int fd = -1) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

class ScopedIgnoredSigpipe {
public:
    ScopedIgnoredSigpipe() {
        struct sigaction action {};
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
    struct sigaction previous_ {};
};

struct Pipe {
    UniqueFd read_end;
    UniqueFd write_end;
};

Pipe create_pipe() {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        throw ValidationError(std::string("cannot create transfer pipe: ") + std::strerror(errno));
    }
    return Pipe{.read_end = UniqueFd(fds[0]), .write_end = UniqueFd(fds[1])};
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        throw ValidationError(std::string("cannot configure transfer pipe: ") + std::strerror(errno));
    }
}

UniqueFd open_dev_null() {
    int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        throw ValidationError(std::string("cannot open /dev/null: ") + std::strerror(errno));
    }
    return UniqueFd(fd);
}

ProcessSpawnResult spawn_transfer_process(
    const std::vector<std::string>& argv,
    int stdin_fd,
    int stdout_fd,
    int stderr_fd,
    const std::vector<int>& inherited_fds
) {
    if (argv.empty()) {
        throw ValidationError("empty transfer command");
    }
    return spawn_program(argv, {
        .stdin_fd = stdin_fd,
        .stdout_fd = stdout_fd,
        .stderr_fd = stderr_fd,
        .create_process_group = true,
        .inherited_fds = inherited_fds,
    });
}

int status_to_exit_code(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 128;
}

bool reap_child(pid_t pid, TransferSideResult& result) {
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, WNOHANG);
    } while (waited < 0 && errno == EINTR);
    if (waited == 0) {
        return false;
    }
    if (waited < 0) {
        result.exit_code = 128;
        result.diagnostics = std::string("waitpid failed: ") + std::strerror(errno);
        return true;
    }
    result.exit_code = status_to_exit_code(status);
    return true;
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

void read_available(int fd, std::string& output) {
    char buffer[4096];
    while (true) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
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
        output.append("read failed: ");
        output.append(std::strerror(errno));
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
    const long double rate = static_cast<long double>(bytes) * 1000.0L
        / static_cast<long double>(elapsed_ms);
    return rate >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(rate);
}

void emit_event(
    ITransferEventSink& events,
    TransferEventKind kind,
    const TransferResult& result,
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

    void maybe_report(ITransferEventSink& events, const TransferResult& result) {
        const SteadyClock::time_point now = SteadyClock::now();
        if (now - last_report_at_ < report_interval_) {
            return;
        }
        report(events, result, now);
    }

    void flush(ITransferEventSink& events, const TransferResult& result) {
        if (reported_ && result.bytes_transferred == last_reported_bytes_) {
            return;
        }
        report(events, result, SteadyClock::now());
    }

private:
    void report(ITransferEventSink& events, const TransferResult& result, SteadyClock::time_point now) {
        const std::uint64_t elapsed_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at_).count()
        );
        const std::uint64_t delta_bytes = result.bytes_transferred >= last_reported_bytes_
            ? result.bytes_transferred - last_reported_bytes_
            : 0;
        const std::uint64_t smoothed_speed = speed_.sample(result.bytes_transferred, elapsed_ms);
        emit_event(
            events,
            TransferEventKind::Progress,
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
    TransferSpeedEstimator speed_;
    std::uint64_t last_reported_bytes_ = 0;
    bool reported_ = false;
};

std::string side_failure(const char* side, const TransferSideResult& result) {
    std::string message = std::string(side) + " failed";
    if (!result.started) {
        message += " before start";
    } else {
        message += " with exit code " + std::to_string(result.exit_code);
    }
    if (!result.diagnostics.empty()) {
        message += ": " + result.diagnostics;
    }
    return message;
}

class ThreadedAsyncTransferHandle final : public IAsyncTransferHandle {
public:
    ThreadedAsyncTransferHandle(
        std::shared_ptr<CancellationToken> cancellation,
        UniqueFd completion_read,
        std::future<TransferResult> future
    )
        : cancellation_(std::move(cancellation)),
          completion_read_(std::move(completion_read)),
          future_(std::move(future)) {
    }

    ThreadedAsyncTransferHandle(const ThreadedAsyncTransferHandle&) = delete;
    ThreadedAsyncTransferHandle& operator=(const ThreadedAsyncTransferHandle&) = delete;

    ~ThreadedAsyncTransferHandle() override {
        if (future_.valid() && !result_.has_value()) {
            request_cancel();
            try {
                result_ = future_.get();
            } catch (...) {
            }
        }
    }

    bool finished() const override {
        if (result_.has_value()) {
            return true;
        }
        return future_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    }

    int completion_fd() const override {
        return completion_read_.get();
    }

    void request_cancel() override {
        cancellation_->request_cancel();
    }

    TransferResult wait() override {
        if (!result_.has_value()) {
            result_ = future_.get();
        }
        return *result_;
    }

private:
    std::shared_ptr<CancellationToken> cancellation_;
    UniqueFd completion_read_;
    mutable std::future<TransferResult> future_;
    std::optional<TransferResult> result_;
};

} // namespace

std::uint64_t TransferSpeedEstimator::sample(
    std::uint64_t bytes_transferred,
    std::uint64_t elapsed_ms
) {
    const auto bounded_speed = [](double speed) {
        if (speed <= 0) {
            return std::uint64_t{0};
        }
        if (speed >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return static_cast<std::uint64_t>(speed);
    };
    if (elapsed_ms <= previous_elapsed_ms_) {
        return bounded_speed(smoothed_speed_bps_);
    }

    const std::uint64_t delta_bytes = bytes_transferred >= previous_bytes_
        ? bytes_transferred - previous_bytes_
        : bytes_transferred;
    const std::uint64_t delta_ms = elapsed_ms - previous_elapsed_ms_;
    const double instantaneous_speed = static_cast<double>(delta_bytes) * 1000.0
        / static_cast<double>(delta_ms);
    if (!initialized_) {
        smoothed_speed_bps_ = instantaneous_speed;
        initialized_ = true;
    } else {
        constexpr double smoothing_period_ms = 3000.0;
        const double alpha = -std::expm1(-static_cast<double>(delta_ms) / smoothing_period_ms);
        smoothed_speed_bps_ += alpha * (instantaneous_speed - smoothed_speed_bps_);
    }

    previous_bytes_ = bytes_transferred;
    previous_elapsed_ms_ = elapsed_ms;
    return bounded_speed(smoothed_speed_bps_);
}

ThreadedAsyncTransferPipeline::ThreadedAsyncTransferPipeline(ITransferPipeline& pipeline)
    : pipeline_(pipeline) {
}

std::unique_ptr<IAsyncTransferHandle> ThreadedAsyncTransferPipeline::start(
    const TransferPipelinePlan& plan,
    ITransferEventSink& events
) {
    auto cancellation = std::make_shared<CancellationToken>();
    Pipe completion_pipe = create_pipe();
    std::future<TransferResult> future = std::async(
        std::launch::async,
        [this, plan, &events, cancellation, completion_write = std::move(completion_pipe.write_end)]() mutable {
            try {
                TransferResult result = pipeline_.run(plan, events, *cancellation);
                char byte = 1;
                ssize_t ignored = write(completion_write.get(), &byte, sizeof(byte));
                (void)ignored;
                return result;
            } catch (...) {
                char byte = 1;
                ssize_t ignored = write(completion_write.get(), &byte, sizeof(byte));
                (void)ignored;
                throw;
            }
        }
    );
    completion_pipe.write_end.reset();
    set_nonblocking(completion_pipe.read_end.get());
    return std::make_unique<ThreadedAsyncTransferHandle>(
        std::move(cancellation),
        std::move(completion_pipe.read_end),
        std::move(future)
    );
}

PosixTransferPipeline::PosixTransferPipeline(TransferTerminationPolicy termination_policy)
    : termination_policy_(termination_policy) {
    if (termination_policy_.terminate_grace_period.count() <= 0
        || termination_policy_.kill_reap_period.count() <= 0) {
        throw ValidationError("transfer termination periods must be positive");
    }
}

TransferResult PosixTransferPipeline::run(
    const TransferPipelinePlan& plan,
    ITransferEventSink& events,
    CancellationToken& cancellation
) {
    const auto started_at = SteadyClock::now();
    TransferProgressReporter progress_reporter(started_at);
    ScopedIgnoredSigpipe ignored_sigpipe;
    Pipe data_pipe = create_pipe();
    Pipe consumer_input_pipe = create_pipe();
    Pipe producer_error_pipe = create_pipe();
    Pipe consumer_error_pipe = create_pipe();
    UniqueFd dev_null = open_dev_null();

    ProcessSpawnResult producer_spawn = spawn_transfer_process(
        plan.producer_argv,
        dev_null.get(),
        data_pipe.write_end.get(),
        producer_error_pipe.write_end.get(),
        plan.inherited_fds
    );
    ChildProcess producer_process(
        producer_spawn.started() ? producer_spawn.pid : -1,
        true,
        {
            .terminate_grace_period = termination_policy_.terminate_grace_period,
            .kill_reap_period = termination_policy_.kill_reap_period,
        }
    );
    ProcessSpawnResult consumer_spawn = spawn_transfer_process(
        plan.consumer_argv,
        consumer_input_pipe.read_end.get(),
        dev_null.get(),
        consumer_error_pipe.write_end.get(),
        plan.inherited_fds
    );
    ChildProcess consumer_process(
        consumer_spawn.started() ? consumer_spawn.pid : -1,
        true,
        {
            .terminate_grace_period = termination_policy_.terminate_grace_period,
            .kill_reap_period = termination_policy_.kill_reap_period,
        }
    );

    TransferResult result;
    result.producer.started = producer_spawn.started();
    result.consumer.started = consumer_spawn.started();
    if (!result.producer.started) {
        result.producer.diagnostics = "posix_spawn failed for " + plan.producer_argv.front()
            + ": " + std::strerror(producer_spawn.error);
    }
    if (!result.consumer.started) {
        result.consumer.diagnostics = "posix_spawn failed for " + plan.consumer_argv.front()
            + ": " + std::strerror(consumer_spawn.error);
    }
    result.bytes_total_estimated = plan.bytes_total_estimated;
    emit_event(events, TransferEventKind::Started, result, started_at);

    data_pipe.write_end.reset();
    consumer_input_pipe.read_end.reset();
    producer_error_pipe.write_end.reset();
    consumer_error_pipe.write_end.reset();

    if (result.producer.started) {
        set_nonblocking(data_pipe.read_end.get());
        set_nonblocking(producer_error_pipe.read_end.get());
    } else {
        data_pipe.read_end.reset();
        producer_error_pipe.read_end.reset();
    }
    if (result.consumer.started) {
        set_nonblocking(consumer_input_pipe.write_end.get());
        set_nonblocking(consumer_error_pipe.read_end.get());
    } else {
        consumer_input_pipe.write_end.reset();
        consumer_error_pipe.read_end.reset();
    }

    bool producer_stdout_open = result.producer.started;
    bool consumer_stdin_open = result.consumer.started;
    bool producer_stdout_ready = false;
    bool consumer_stdin_ready = false;
    bool producer_stderr_open = result.producer.started;
    bool consumer_stderr_open = result.consumer.started;
    bool producer_done = !result.producer.started;
    bool consumer_done = !result.consumer.started;
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
        if (state.terminate_sent || state.process == nullptr || state.process->pid() <= 0
            || (done && !state.process->process_group_exists())) {
            return;
        }
        state.process->send_signal(SIGTERM);
        state.terminate_sent = true;
        state.deadline = SteadyClock::now() + termination_policy_.terminate_grace_period;
    };
    auto advance_termination = [&] (
        ChildTerminationState& state,
        bool& done,
        TransferSideResult& side
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
            append_diagnostic(side.diagnostics, "did not exit after SIGTERM; sent SIGKILL");
            return ChildTerminationProgress::None;
        }

        state.deadline.reset();
        if (done) {
            return ChildTerminationProgress::Abandoned;
        }
        if (reap_child(state.process->pid(), side)) {
            done = true;
            state.process->mark_reaped();
            return ChildTerminationProgress::Reaped;
        }
        side.exit_code = 128 + SIGKILL;
        append_diagnostic(side.diagnostics, "did not become waitable after SIGKILL");
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
        emit_event(events, TransferEventKind::Cancelled, result, started_at);
        cancellation_sent = true;
    };

    auto termination_pending = [](const ChildTerminationState& state) {
        return state.terminate_sent && state.deadline.has_value();
    };
    while (!producer_done || !consumer_done || producer_stdout_open || producer_stderr_open || consumer_stderr_open
        || termination_pending(producer_termination) || termination_pending(consumer_termination)) {
        if (cancellation.cancellation_requested()) {
            cancel_transfer();
        }
        ChildTerminationProgress producer_progress = advance_termination(
            producer_termination,
            producer_done,
            result.producer
        );
        if (producer_progress == ChildTerminationProgress::Reaped) {
            emit_event(events, TransferEventKind::ProducerFinished, result, started_at);
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
            emit_event(events, TransferEventKind::ConsumerFinished, result, started_at);
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
        if (!cancellation_sent && cancellation.cancellation_fd() >= 0) {
            fds.push_back({.fd = cancellation.cancellation_fd(), .events = POLLIN, .revents = 0});
            tags.push_back(cancellation_tag);
        }

        if (!fds.empty()) {
            int ready = poll(fds.data(), fds.size(), 100);
            if (ready < 0 && errno != EINTR) {
                const std::string message = std::string("transfer poll failed: ") + std::strerror(errno);
                append_diagnostic(result.producer.diagnostics, message);
                append_diagnostic(result.consumer.diagnostics, message);
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
                append_diagnostic(result.producer.diagnostics, "stdout pipe became invalid");
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
                append_diagnostic(result.consumer.diagnostics, "stdin closed before transfer completed");
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
                read_available(producer_error_pipe.read_end.get(), result.producer.diagnostics);
                if ((fds[i].revents & POLLHUP) != 0) {
                    producer_error_pipe.read_end.reset();
                    producer_stderr_open = false;
                }
            } else if (tag == consumer_stderr_tag && (fds[i].revents & (POLLIN | POLLHUP)) != 0) {
                read_available(consumer_error_pipe.read_end.get(), result.consumer.diagnostics);
                if ((fds[i].revents & POLLHUP) != 0) {
                    consumer_error_pipe.read_end.reset();
                    consumer_stderr_open = false;
                }
            } else if (tag == cancellation_tag && (fds[i].revents & POLLIN) != 0) {
                cancellation.drain_cancellation_signal();
                cancel_transfer();
            }
        }

        constexpr std::size_t splice_chunk_bytes = 1024U * 1024U;
        constexpr std::size_t splice_cycle_budget_bytes = 16U * 1024U * 1024U;
        std::size_t cycle_bytes = 0;
        while (producer_stdout_open && consumer_stdin_open && producer_stdout_ready && consumer_stdin_ready) {
            ssize_t count = splice(
                data_pipe.read_end.get(),
                nullptr,
                consumer_input_pipe.write_end.get(),
                nullptr,
                splice_chunk_bytes,
                SPLICE_F_NONBLOCK | SPLICE_F_MORE
            );
            if (count > 0) {
                const auto transferred = static_cast<std::uint64_t>(count);
                result.bytes_produced += transferred;
                result.bytes_transferred += transferred;
                cycle_bytes += static_cast<std::size_t>(count);
                if (cycle_bytes >= splice_cycle_budget_bytes) {
                    producer_stdout_ready = false;
                    consumer_stdin_ready = false;
                }
                continue;
            }
            if (count == 0) {
                data_pipe.read_end.reset();
                producer_stdout_open = false;
                producer_stdout_ready = false;
                consumer_input_pipe.write_end.reset();
                consumer_stdin_open = false;
                consumer_stdin_ready = false;
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                producer_stdout_ready = false;
                consumer_stdin_ready = false;
                break;
            }

            append_diagnostic(result.consumer.diagnostics, std::string("stream splice failed: ") + std::strerror(errno));
            consumer_input_pipe.write_end.reset();
            consumer_stdin_open = false;
            consumer_stdin_ready = false;
            data_pipe.read_end.reset();
            producer_stdout_open = false;
            producer_stdout_ready = false;
            request_termination(producer_termination, producer_done);
            request_termination(consumer_termination, consumer_done);
            break;
        }

        progress_reporter.maybe_report(events, result);

        if (!producer_done && reap_child(producer_pid, result.producer)) {
            producer_done = true;
            producer_process.mark_reaped();
            emit_event(events, TransferEventKind::ProducerFinished, result, started_at);
        }
        if (!consumer_done && reap_child(consumer_pid, result.consumer)) {
            consumer_done = true;
            consumer_process.mark_reaped();
            emit_event(events, TransferEventKind::ConsumerFinished, result, started_at);
        }
    }

    progress_reporter.flush(events, result);
    trim_diagnostics(result.producer.diagnostics);
    trim_diagnostics(result.consumer.diagnostics);
    result.duration_ms = elapsed_ms_since(started_at);
    result.average_speed_bps = average_speed_bps(result.bytes_transferred, result.duration_ms);
    emit_event(events, TransferEventKind::Completed, result, started_at);
    return result;
}

void require_transfer_success(const TransferResult& result) {
    if (transfer_succeeded(result)) {
        return;
    }
    if (result.cancelled) {
        throw ValidationError("Transfer was cancelled");
    }

    const bool producer_failed = !result.producer.started || result.producer.exit_code != 0;
    const bool consumer_failed = !result.consumer.started || result.consumer.exit_code != 0;
    if (producer_failed && consumer_failed) {
        throw ValidationError(
            "Transfer failed: "
            + side_failure("producer", result.producer)
            + "; "
            + side_failure("consumer", result.consumer)
        );
    }
    if (producer_failed) {
        throw ValidationError(side_failure("producer", result.producer));
    }
    if (consumer_failed) {
        throw ValidationError(side_failure("consumer", result.consumer));
    }
    throw ValidationError("Transfer failed");
}

} // namespace btrfsbackup
