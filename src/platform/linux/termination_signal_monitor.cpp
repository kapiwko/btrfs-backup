// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/termination_signal_monitor.hpp>

#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>

namespace btrfsbackup::platform_linux {

class TerminationSignalMonitor::Impl {
public:
    explicit Impl(std::function<void()> on_termination)
        : on_termination_(std::move(on_termination)) {
        sigemptyset(&signals_);
        sigaddset(&signals_, SIGINT);
        sigaddset(&signals_, SIGTERM);

        int mask_error = pthread_sigmask(SIG_BLOCK, &signals_, &previous_mask_);
        if (mask_error != 0) {
            throw std::runtime_error(std::string("cannot block termination signals: ") + std::strerror(mask_error));
        }
        mask_installed_ = true;

        signal_fd_ = signalfd(-1, &signals_, SFD_CLOEXEC | SFD_NONBLOCK);
        if (signal_fd_ < 0) {
            int signal_fd_error = errno;
            cleanup();
            throw std::runtime_error(std::string("cannot create termination signal fd: ") + std::strerror(signal_fd_error));
        }
        stop_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (stop_fd_ < 0) {
            int stop_fd_error = errno;
            cleanup();
            throw std::runtime_error(std::string("cannot create signal monitor stop fd: ") + std::strerror(stop_fd_error));
        }

        try {
            worker_ = std::thread([this] { run(); });
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~Impl() {
        std::uint64_t value = 1;
        ssize_t ignored = write(stop_fd_, &value, sizeof(value));
        (void)ignored;
        if (worker_.joinable()) {
            worker_.join();
        }
        cleanup();
    }

private:
    void run() {
        pollfd fds[2]{
            {.fd = signal_fd_, .events = POLLIN, .revents = 0},
            {.fd = stop_fd_, .events = POLLIN, .revents = 0},
        };
        while (true) {
            int ready = poll(fds, 2, -1);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                on_termination_();
                return;
            }
            if ((fds[1].revents & POLLIN) != 0) {
                return;
            }
            if ((fds[0].revents & POLLIN) == 0) {
                continue;
            }

            signalfd_siginfo signal_info {};
            while (read(signal_fd_, &signal_info, sizeof(signal_info)) == sizeof(signal_info)) {
                if (signal_info.ssi_signo == SIGINT || signal_info.ssi_signo == SIGTERM) {
                    on_termination_();
                }
            }
        }
    }

    void cleanup() {
        if (stop_fd_ >= 0) {
            close(stop_fd_);
            stop_fd_ = -1;
        }
        if (signal_fd_ >= 0) {
            close(signal_fd_);
            signal_fd_ = -1;
        }
        if (mask_installed_) {
            pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
            mask_installed_ = false;
        }
    }

    std::function<void()> on_termination_;
    sigset_t signals_ {};
    sigset_t previous_mask_ {};
    bool mask_installed_ = false;
    int signal_fd_ = -1;
    int stop_fd_ = -1;
    std::thread worker_;
};

TerminationSignalMonitor::TerminationSignalMonitor(std::function<void()> on_termination)
    : impl_(std::make_unique<Impl>(std::move(on_termination))) {
}

TerminationSignalMonitor::~TerminationSignalMonitor() = default;

} // namespace btrfsbackup::platform_linux
