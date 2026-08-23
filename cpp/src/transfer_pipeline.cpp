#include <btrfsbackup/transfer_pipeline.hpp>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>

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

std::vector<char*> argv_for_exec(const std::vector<std::string>& argv) {
    std::vector<char*> result;
    result.reserve(argv.size() + 1);
    for (const std::string& item : argv) {
        result.push_back(const_cast<char*>(item.c_str()));
    }
    result.push_back(nullptr);
    return result;
}

pid_t spawn_process(
    const std::vector<std::string>& argv,
    int stdin_fd,
    int stdout_fd,
    int stderr_fd
) {
    if (argv.empty()) {
        throw ValidationError("empty transfer command");
    }

    pid_t pid = fork();
    if (pid < 0) {
        throw ValidationError(std::string("cannot fork transfer process: ") + std::strerror(errno));
    }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(stdin_fd, STDIN_FILENO);
        dup2(stdout_fd, STDOUT_FILENO);
        dup2(stderr_fd, STDERR_FILENO);
        std::vector<char*> args = argv_for_exec(argv);
        execvp(args[0], args.data());
        _exit(127);
    }
    setpgid(pid, pid);
    return pid;
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
    pid_t waited = waitpid(pid, &status, WNOHANG);
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

void terminate_child(pid_t pid) {
    if (pid > 0) {
        if (kill(-pid, SIGTERM) != 0 && errno == ESRCH) {
            kill(pid, SIGTERM);
        }
    }
}

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

std::uint64_t speed_bps(std::uint64_t bytes, std::uint64_t elapsed_ms) {
    if (elapsed_ms == 0) {
        return 0;
    }
    return bytes * 1000 / elapsed_ms;
}

void emit_event(
    ITransferEventSink& events,
    TransferEventKind kind,
    const TransferResult& result,
    SteadyClock::time_point started_at,
    std::uint64_t delta_bytes = 0,
    std::uint64_t pending_bytes = 0,
    const std::string& message = ""
) {
    std::uint64_t elapsed = elapsed_ms_since(started_at);
    events.on_transfer_event({
        .kind = kind,
        .bytes_transferred = result.bytes_transferred,
        .bytes_produced = result.bytes_produced,
        .bytes_total_estimated = result.bytes_total_estimated,
        .delta_bytes = delta_bytes,
        .pending_bytes = pending_bytes,
        .elapsed_ms = elapsed,
        .speed_bps = speed_bps(result.bytes_transferred, elapsed),
        .message = message,
    });
}

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

TransferResult PosixTransferPipeline::run(
    const TransferPipelinePlan& plan,
    ITransferEventSink& events,
    CancellationToken& cancellation
) {
    const auto started_at = SteadyClock::now();
    ScopedIgnoredSigpipe ignored_sigpipe;
    Pipe data_pipe = create_pipe();
    Pipe consumer_input_pipe = create_pipe();
    Pipe producer_error_pipe = create_pipe();
    Pipe consumer_error_pipe = create_pipe();
    UniqueFd dev_null = open_dev_null();

    pid_t producer_pid = spawn_process(
        plan.producer_argv,
        dev_null.get(),
        data_pipe.write_end.get(),
        producer_error_pipe.write_end.get()
    );
    pid_t consumer_pid = spawn_process(
        plan.consumer_argv,
        consumer_input_pipe.read_end.get(),
        dev_null.get(),
        consumer_error_pipe.write_end.get()
    );

    TransferResult result;
    result.producer.started = true;
    result.consumer.started = true;
    result.bytes_total_estimated = plan.bytes_total_estimated;
    emit_event(events, TransferEventKind::Started, result, started_at);

    data_pipe.write_end.reset();
    consumer_input_pipe.read_end.reset();
    producer_error_pipe.write_end.reset();
    consumer_error_pipe.write_end.reset();
    set_nonblocking(data_pipe.read_end.get());
    set_nonblocking(consumer_input_pipe.write_end.get());
    set_nonblocking(producer_error_pipe.read_end.get());
    set_nonblocking(consumer_error_pipe.read_end.get());

    std::string pending;
    bool producer_stdout_open = true;
    bool consumer_stdin_open = true;
    bool producer_stderr_open = true;
    bool consumer_stderr_open = true;
    bool producer_done = false;
    bool consumer_done = false;
    bool cancellation_sent = false;
    auto cancel_transfer = [&] {
        if (cancellation_sent) {
            return;
        }
        result.cancelled = true;
        terminate_child(producer_pid);
        terminate_child(consumer_pid);
        if (consumer_stdin_open) {
            consumer_input_pipe.write_end.reset();
            consumer_stdin_open = false;
        }
        emit_event(events, TransferEventKind::Cancelled, result, started_at, 0, pending.size());
        cancellation_sent = true;
    };

    while (!producer_done || !consumer_done || producer_stdout_open || producer_stderr_open || consumer_stderr_open || !pending.empty()) {
        if (cancellation.cancellation_requested()) {
            cancel_transfer();
        }

        std::vector<pollfd> fds;
        std::vector<int> tags;
        constexpr int producer_stdout_tag = 1;
        constexpr int consumer_stdin_tag = 2;
        constexpr int producer_stderr_tag = 3;
        constexpr int consumer_stderr_tag = 4;
        constexpr int cancellation_tag = 5;

        if (producer_stdout_open) {
            fds.push_back({.fd = data_pipe.read_end.get(), .events = POLLIN | POLLHUP, .revents = 0});
            tags.push_back(producer_stdout_tag);
        }
        if (consumer_stdin_open && !pending.empty()) {
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
                terminate_child(producer_pid);
                terminate_child(consumer_pid);
                throw ValidationError(std::string("transfer poll failed: ") + std::strerror(errno));
            }
        }

        for (std::size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].revents == 0) {
                continue;
            }
            int tag = tags[i];
            if (tag == producer_stdout_tag && (fds[i].revents & (POLLIN | POLLHUP)) != 0) {
                char buffer[65536];
                while (true) {
                    ssize_t count = read(data_pipe.read_end.get(), buffer, sizeof(buffer));
                    if (count > 0) {
                        pending.append(buffer, static_cast<std::size_t>(count));
                        result.bytes_produced += static_cast<std::uint64_t>(count);
                        continue;
                    }
                    if (count == 0) {
                        data_pipe.read_end.reset();
                        producer_stdout_open = false;
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    result.producer.diagnostics = std::string("stdout read failed: ") + std::strerror(errno);
                    data_pipe.read_end.reset();
                    producer_stdout_open = false;
                    break;
                }
            } else if (tag == consumer_stdin_tag && (fds[i].revents & (POLLHUP | POLLERR)) != 0) {
                append_diagnostic(result.consumer.diagnostics, "stdin closed before transfer completed");
                consumer_input_pipe.write_end.reset();
                consumer_stdin_open = false;
                pending.clear();
                if (producer_stdout_open) {
                    data_pipe.read_end.reset();
                    producer_stdout_open = false;
                }
                terminate_child(producer_pid);
            } else if (tag == consumer_stdin_tag && (fds[i].revents & POLLOUT) != 0) {
                ssize_t count = write(consumer_input_pipe.write_end.get(), pending.data(), pending.size());
                if (count > 0) {
                    result.bytes_transferred += static_cast<std::uint64_t>(count);
                    pending.erase(0, static_cast<std::size_t>(count));
                    emit_event(
                        events,
                        TransferEventKind::Progress,
                        result,
                        started_at,
                        static_cast<std::uint64_t>(count),
                        pending.size()
                    );
                } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    append_diagnostic(result.consumer.diagnostics, std::string("stdin write failed: ") + std::strerror(errno));
                    consumer_input_pipe.write_end.reset();
                    consumer_stdin_open = false;
                    pending.clear();
                    if (producer_stdout_open) {
                        data_pipe.read_end.reset();
                        producer_stdout_open = false;
                    }
                    terminate_child(producer_pid);
                }
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

        if (!producer_stdout_open && pending.empty() && consumer_stdin_open) {
            consumer_input_pipe.write_end.reset();
            consumer_stdin_open = false;
        }

        if (!producer_done && reap_child(producer_pid, result.producer)) {
            producer_done = true;
            emit_event(events, TransferEventKind::ProducerFinished, result, started_at, 0, pending.size());
        }
        if (!consumer_done && reap_child(consumer_pid, result.consumer)) {
            consumer_done = true;
            emit_event(events, TransferEventKind::ConsumerFinished, result, started_at, 0, pending.size());
        }
    }

    trim_diagnostics(result.producer.diagnostics);
    trim_diagnostics(result.consumer.diagnostics);
    result.duration_ms = elapsed_ms_since(started_at);
    result.average_speed_bps = speed_bps(result.bytes_transferred, result.duration_ms);
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
