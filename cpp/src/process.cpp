#include <btrfsbackup/process.hpp>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/process_spawn.hpp>

namespace btrfsbackup {

namespace {

constexpr std::size_t max_drain_bytes_per_poll = 256 * 1024;

void append_bounded(std::string& output, const char* data, std::size_t size, std::size_t limit) {
    if (output.size() >= limit) {
        return;
    }
    const std::size_t available = limit - output.size();
    output.append(data, std::min(size, available));
}

bool fd_is_ready(int fd) {
    if (fd < 0) {
        return false;
    }
    pollfd descriptor{.fd = fd, .events = POLLIN, .revents = 0};
    int result;
    do {
        result = poll(&descriptor, 1, 0);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

} // namespace

CommandResult run_command(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        throw ValidationError("empty command");
    }
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        throw ValidationError("cannot create pipe");
    }
    ProcessSpawnResult spawned = spawn_program(argv, {
        .stdout_fd = pipefd[1],
        .stderr_fd = pipefd[1],
        .inherited_fds = {},
    });
    close(pipefd[1]);
    CommandResult result;
    if (!spawned.started()) {
        close(pipefd[0]);
        result.exit_code = 127;
        result.output = "cannot spawn " + argv.front() + ": " + std::strerror(spawned.error);
        return result;
    }
    ChildProcess child(spawned.pid, false);
    char buffer[4096];
    while (true) {
        ssize_t count = read(pipefd[0], buffer, sizeof(buffer));
        if (count > 0) {
            result.output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        result.output += std::string("cannot read command output: ") + std::strerror(errno);
        break;
    }
    close(pipefd[0]);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(spawned.pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        throw ValidationError(std::string("cannot wait for command: ") + std::strerror(errno));
    }
    child.mark_reaped();
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = 128;
    }
    return result;
}

CommandResult run_controlled_command(
    const std::vector<std::string>& argv,
    const ControlledCommandOptions& options
) {
    if (argv.empty()) {
        throw ValidationError("empty command");
    }
    if (options.timeout <= std::chrono::milliseconds::zero()) {
        throw ValidationError("command timeout must be positive");
    }

    CommandResult result;
    if (fd_is_ready(options.cancellation_fd)) {
        result.cancelled = true;
        return result;
    }

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        throw ValidationError(std::string("cannot create command pipe: ") + std::strerror(errno));
    }
    int read_flags = fcntl(pipefd[0], F_GETFL);
    if (read_flags < 0 || fcntl(pipefd[0], F_SETFL, read_flags | O_NONBLOCK) != 0) {
        const int error = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        throw ValidationError(std::string("cannot configure command pipe: ") + std::strerror(error));
    }
    ProcessSpawnResult spawned = spawn_program(argv, {
        .stdout_fd = pipefd[1],
        .stderr_fd = pipefd[1],
        .create_process_group = true,
        .inherited_fds = {},
    });
    close(pipefd[1]);
    if (!spawned.started()) {
        close(pipefd[0]);
        result.exit_code = 127;
        result.output = "cannot spawn " + argv.front() + ": " + std::strerror(spawned.error);
        return result;
    }

    ChildProcess child(
        spawned.pid,
        true,
        {
            .terminate_grace_period = options.terminate_grace_period,
            .kill_reap_period = options.kill_reap_period,
        }
    );
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    bool output_open = true;
    bool child_reaped = false;
    int child_status = 0;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            result.timed_out = true;
            close(pipefd[0]);
            return result;
        }

        pollfd descriptors[2];
        nfds_t count = 0;
        if (output_open) {
            descriptors[count++] = {.fd = pipefd[0], .events = POLLIN, .revents = 0};
        }
        if (options.cancellation_fd >= 0) {
            descriptors[count++] = {.fd = options.cancellation_fd, .events = POLLIN, .revents = 0};
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const int poll_timeout = static_cast<int>(std::min(remaining, std::chrono::milliseconds(100)).count());
        int polled;
        do {
            polled = poll(descriptors, count, poll_timeout);
        } while (polled < 0 && errno == EINTR);
        if (polled < 0) {
            close(pipefd[0]);
            throw ValidationError(std::string("cannot poll command: ") + std::strerror(errno));
        }

        nfds_t index = 0;
        if (output_open) {
            const short events = descriptors[index++].revents;
            if ((events & POLLNVAL) != 0) {
                close(pipefd[0]);
                throw ValidationError("command output descriptor became invalid");
            }
            if ((events & (POLLIN | POLLHUP | POLLERR)) != 0) {
                char buffer[4096];
                std::size_t drained_bytes = 0;
                while (drained_bytes < max_drain_bytes_per_poll) {
                    const ssize_t bytes = read(pipefd[0], buffer, sizeof(buffer));
                    if (bytes > 0) {
                        drained_bytes += static_cast<std::size_t>(bytes);
                        append_bounded(result.output, buffer, static_cast<std::size_t>(bytes), options.max_output_bytes);
                        continue;
                    }
                    if (bytes == 0) {
                        close(pipefd[0]);
                        output_open = false;
                    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        close(pipefd[0]);
                        throw ValidationError(std::string("cannot read command output: ") + std::strerror(errno));
                    }
                    break;
                }
            }
        }
        if (options.cancellation_fd >= 0) {
            const short events = descriptors[index].revents;
            if ((events & POLLNVAL) != 0) {
                if (output_open) {
                    close(pipefd[0]);
                }
                throw ValidationError("command cancellation descriptor became invalid");
            }
            if ((events & (POLLIN | POLLHUP | POLLERR)) != 0) {
                if (output_open) {
                    close(pipefd[0]);
                }
                result.cancelled = true;
                return result;
            }
        }

        if (!child_reaped) {
            pid_t waited;
            do {
                waited = waitpid(spawned.pid, &child_status, WNOHANG);
            } while (waited < 0 && errno == EINTR);
            if (waited == spawned.pid) {
                child_reaped = true;
            } else if (waited < 0) {
                if (output_open) {
                    close(pipefd[0]);
                }
                throw ValidationError(std::string("cannot wait for command: ") + std::strerror(errno));
            }
        }

        if (child_reaped && !output_open) {
            child.mark_reaped();
            if (WIFEXITED(child_status)) {
                result.exit_code = WEXITSTATUS(child_status);
            } else if (WIFSIGNALED(child_status)) {
                result.exit_code = 128 + WTERMSIG(child_status);
            } else {
                result.exit_code = 128;
            }
            return result;
        }
    }
}

std::string run_capture(const std::vector<std::string>& argv) {
    CommandResult result = run_command(argv);
    if (result.exit_code != 0) {
        throw ValidationError("command failed: " + argv.front());
    }
    while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r')) {
        result.output.pop_back();
    }
    return result.output;
}

} // namespace btrfsbackup
