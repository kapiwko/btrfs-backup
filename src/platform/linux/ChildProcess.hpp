// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <sys/types.h>

#include <chrono>

namespace btrfsbackup::platform::linux {

struct ChildProcessCleanupPolicy {
    std::chrono::milliseconds terminate_grace_period{5000};
    std::chrono::milliseconds kill_reap_period{5000};
};

class ChildProcess {
  public:
    ChildProcess() = default;
    ChildProcess(pid_t pid, bool process_group, ChildProcessCleanupPolicy cleanup_policy = {}) noexcept;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;
    ~ChildProcess();

    pid_t pid() const;
    bool process_group_exists() const;
    void send_signal(int signal) const;
    void mark_reaped();
    void release();

  private:
    void cleanup() noexcept;
    bool wait_until(std::chrono::steady_clock::time_point deadline) noexcept;

    pid_t pid_ = -1;
    bool process_group_ = false;
    bool owned_ = false;
    bool leader_reaped_ = false;
    ChildProcessCleanupPolicy cleanup_policy_;
};

} // namespace btrfsbackup::platform::linux
