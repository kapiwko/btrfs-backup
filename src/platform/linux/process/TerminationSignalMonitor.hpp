// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>

namespace btrfsbackup::platform::linux::process {

class TerminationSignalMonitor {
  public:
    explicit TerminationSignalMonitor(std::function<void()> on_termination);
    TerminationSignalMonitor(const TerminationSignalMonitor&) = delete;
    TerminationSignalMonitor& operator=(const TerminationSignalMonitor&) = delete;
    ~TerminationSignalMonitor() noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace btrfsbackup::platform::linux::process
