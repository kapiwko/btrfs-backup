// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <signal.h>

namespace btrfsbackup::platform::linux::transfer {

class ThreadSigpipeBlock final {
  public:
    ThreadSigpipeBlock();
    ThreadSigpipeBlock(const ThreadSigpipeBlock&) = delete;
    ThreadSigpipeBlock& operator=(const ThreadSigpipeBlock&) = delete;
    ~ThreadSigpipeBlock() noexcept;

  private:
    sigset_t previous_mask_{};
    bool previously_blocked_ = false;
};

} // namespace btrfsbackup::platform::linux::transfer
