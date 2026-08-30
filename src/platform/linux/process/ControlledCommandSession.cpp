// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/process/ControlledCommandSession.hpp>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <optional>

#include <core/Errors.hpp>
#include <platform/linux/process/ChildProcess.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>
#include <platform/linux/process/CommandCancellationSignal.hpp>
#include <platform/linux/process/ProcessEnvironment.hpp>
#include <platform/linux/process/ProcessSpawn.hpp>

namespace btrfsbackup::platform::linux::process {

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

ProcessEnvironment command_environment(const btrfsbackup::backup::ControlledCommandOptions& options) {
    using btrfsbackup::backup::CommandEnvironmentProfile;
    switch (options.environment_profile) {
    case CommandEnvironmentProfile::Standard:
        if (!options.environment.empty()) {
            throw ValidationError("explicit environment variables require the hook profile");
        }
        return {};
    case CommandEnvironmentProfile::Hook:
        return ProcessEnvironment::for_hook(options.environment);
    case CommandEnvironmentProfile::SystemdControl:
        if (!options.environment.empty()) {
            throw ValidationError("systemd control environment does not accept overrides");
        }
        return ProcessEnvironment::for_systemd_control();
    }
    throw ValidationError("unknown command environment profile");
}

} // namespace

ControlledCommandSession::ControlledCommandSession(
    const std::vector<std::string>& argv,
    const btrfsbackup::backup::ControlledCommandOptions& options
)
    : argv_(argv), options_(options) {
}

btrfsbackup::backup::CommandResult ControlledCommandSession::run() {
    const std::vector<std::string>& argv = argv_;
    const btrfsbackup::backup::ControlledCommandOptions& options = options_;
    if (argv.empty()) {
        throw ValidationError("empty command");
    }
    if (options.timeout <= std::chrono::milliseconds::zero()) {
        throw ValidationError("command timeout must be positive");
    }

    std::optional<CommandCancellationSignal> cancellation_signal;
    if (options.cancellation != nullptr) {
        cancellation_signal.emplace(*options.cancellation);
    }
    const int cancellation_fd = cancellation_signal.has_value() ? cancellation_signal->fd() : -1;

    btrfsbackup::backup::CommandResult result;
    if (fd_is_ready(cancellation_fd)) {
        result.cancelled = true;
        return result;
    }

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        throw ValidationError(std::string("cannot create command pipe: ") + std::strerror(errno));
    }
    OwnedFileDescriptor output_read_end(pipefd[0]);
    OwnedFileDescriptor output_write_end(pipefd[1]);
    int read_flags = fcntl(output_read_end.get(), F_GETFL);
    if (read_flags < 0 || fcntl(output_read_end.get(), F_SETFL, read_flags | O_NONBLOCK) != 0) {
        const int error = errno;
        throw ValidationError(std::string("cannot configure command pipe: ") + std::strerror(error));
    }
    ProcessSpawnResult spawned = spawn_program(argv, {
                                                         .stdout_fd = output_write_end.get(),
                                                         .stderr_fd = output_write_end.get(),
                                                         .create_process_group = true,
                                                         .inherited_fds = options.inherited_fds,
                                                         .environment = command_environment(options),
                                                     });
    output_write_end.reset();
    if (!spawned.started()) {
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
            return result;
        }

        pollfd descriptors[2];
        nfds_t count = 0;
        if (output_open) {
            descriptors[count++] = {.fd = output_read_end.get(), .events = POLLIN, .revents = 0};
        }
        if (cancellation_fd >= 0) {
            descriptors[count++] = {.fd = cancellation_fd, .events = POLLIN, .revents = 0};
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const int poll_timeout = static_cast<int>(std::min(remaining, std::chrono::milliseconds(100)).count());
        int polled;
        do {
            polled = poll(descriptors, count, poll_timeout);
        } while (polled < 0 && errno == EINTR);
        if (polled < 0) {
            throw ValidationError(std::string("cannot poll command: ") + std::strerror(errno));
        }

        nfds_t index = 0;
        if (output_open) {
            const short events = descriptors[index++].revents;
            if ((events & POLLNVAL) != 0) {
                throw ValidationError("command output descriptor became invalid");
            }
            if ((events & (POLLIN | POLLHUP | POLLERR)) != 0) {
                char buffer[4096];
                std::size_t drained_bytes = 0;
                while (drained_bytes < max_drain_bytes_per_poll) {
                    const ssize_t bytes = read(output_read_end.get(), buffer, sizeof(buffer));
                    if (bytes > 0) {
                        drained_bytes += static_cast<std::size_t>(bytes);
                        append_bounded(result.output, buffer, static_cast<std::size_t>(bytes), options.max_output_bytes);
                        continue;
                    }
                    if (bytes == 0) {
                        output_read_end.reset();
                        output_open = false;
                    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        throw ValidationError(std::string("cannot read command output: ") + std::strerror(errno));
                    }
                    break;
                }
            }
        }
        if (cancellation_fd >= 0) {
            const short events = descriptors[index].revents;
            if ((events & POLLNVAL) != 0) {
                throw ValidationError("command cancellation descriptor became invalid");
            }
            if ((events & (POLLIN | POLLHUP | POLLERR)) != 0) {
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

} // namespace btrfsbackup::platform::linux::process
