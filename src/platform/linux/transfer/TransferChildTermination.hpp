// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <optional>

#include <backup/transfer/TransferResult.hpp>
#include <platform/linux/ChildProcess.hpp>

namespace btrfsbackup::platform::linux::transfer {

enum class ChildTerminationProgress {
    None,
    Reaped,
    Abandoned,
};

class TransferChildTermination final {
  public:
    TransferChildTermination(
        ChildProcess& process,
        std::chrono::milliseconds terminate_grace_period,
        std::chrono::milliseconds kill_reap_period
    ) noexcept;

    void request(bool child_done);
    [[nodiscard]] ChildTerminationProgress advance(
        bool& child_done,
        btrfsbackup::backup::transfer::TransferSideResult& side
    );
    [[nodiscard]] bool pending() const noexcept;

  private:
    ChildProcess& process_;
    std::chrono::milliseconds terminate_grace_period_;
    std::chrono::milliseconds kill_reap_period_;
    bool terminate_sent_ = false;
    bool kill_sent_ = false;
    std::optional<std::chrono::steady_clock::time_point> deadline_;
};

} // namespace btrfsbackup::platform::linux::transfer
