// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>

namespace btrfsbackup {

class TransferSpeedEstimator {
  public:
    [[nodiscard]] std::uint64_t sample(
        std::uint64_t bytes_transferred,
        std::chrono::milliseconds elapsed
    );

  private:
    std::uint64_t previous_bytes_ = 0;
    std::chrono::milliseconds previous_elapsed_{0};
    double smoothed_speed_bps_ = 0;
    bool initialized_ = false;
};

} // namespace btrfsbackup
