// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/ChildProcess.hpp>

#include <signal.h>
#include <sys/wait.h>

#include <cerrno>
#include <thread>

namespace btrfsbackup::platform::linux {

ChildProcess::ChildProcess(pid_t pid, bool process_group, ChildProcessCleanupPolicy cleanup_policy) noexcept
    : pid_(pid), process_group_(process_group), owned_(pid > 0), cleanup_policy_(cleanup_policy) {
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : pid_(other.pid_), process_group_(other.process_group_), owned_(other.owned_),
      leader_reaped_(other.leader_reaped_), cleanup_policy_(other.cleanup_policy_) {
    other.release();
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        cleanup();
        pid_ = other.pid_;
        process_group_ = other.process_group_;
        owned_ = other.owned_;
        leader_reaped_ = other.leader_reaped_;
        cleanup_policy_ = other.cleanup_policy_;
        other.release();
    }
    return *this;
}

ChildProcess::~ChildProcess() {
    cleanup();
}

pid_t ChildProcess::pid() const {
    return pid_;
}

bool ChildProcess::process_group_exists() const {
    if (pid_ <= 0) {
        return false;
    }
    pid_t target = process_group_ ? -pid_ : pid_;
    if (kill(target, 0) == 0) {
        return true;
    }
    return errno != ESRCH;
}

void ChildProcess::send_signal(int signal) const {
    if (pid_ <= 0) {
        return;
    }
    pid_t target = process_group_ ? -pid_ : pid_;
    if (kill(target, signal) != 0 && process_group_ && errno == ESRCH) {
        kill(pid_, signal);
    }
}

void ChildProcess::mark_reaped() {
    leader_reaped_ = true;
    owned_ = false;
}

void ChildProcess::release() {
    owned_ = false;
}

bool ChildProcess::wait_until(std::chrono::steady_clock::time_point deadline) noexcept {
    while (true) {
        if (!leader_reaped_) {
            int status = 0;
            pid_t waited;
            do {
                waited = waitpid(pid_, &status, WNOHANG);
            } while (waited < 0 && errno == EINTR);
            if (waited == pid_ || (waited < 0 && errno == ECHILD)) {
                leader_reaped_ = true;
            }
        }
        if (leader_reaped_ && (!process_group_ || !process_group_exists())) {
            owned_ = false;
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ChildProcess::cleanup() noexcept {
    if (!owned_ || pid_ <= 0) {
        return;
    }

    send_signal(SIGTERM);
    if (wait_until(std::chrono::steady_clock::now() + cleanup_policy_.terminate_grace_period)) {
        return;
    }
    send_signal(SIGKILL);
    wait_until(std::chrono::steady_clock::now() + cleanup_policy_.kill_reap_period);

    if (!leader_reaped_) {
        int status = 0;
        pid_t ignored;
        do {
            ignored = waitpid(pid_, &status, WNOHANG);
        } while (ignored < 0 && errno == EINTR);
    }
    owned_ = false;
}

} // namespace btrfsbackup::platform::linux
