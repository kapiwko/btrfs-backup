// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/transfer_speed_estimator.hpp>

#include <cmath>
#include <limits>

namespace btrfsbackup {

std::uint64_t TransferSpeedEstimator::sample(
    std::uint64_t bytes_transferred,
    std::uint64_t elapsed_ms
) {
    const auto bounded_speed = [](double speed) {
        if (speed <= 0) {
            return std::uint64_t{0};
        }
        if (speed >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return static_cast<std::uint64_t>(speed);
    };
    if (elapsed_ms <= previous_elapsed_ms_) {
        return bounded_speed(smoothed_speed_bps_);
    }

    const std::uint64_t delta_bytes = bytes_transferred >= previous_bytes_
        ? bytes_transferred - previous_bytes_
        : bytes_transferred;
    const std::uint64_t delta_ms = elapsed_ms - previous_elapsed_ms_;
    const double instantaneous_speed = static_cast<double>(delta_bytes) * 1000.0
        / static_cast<double>(delta_ms);
    if (!initialized_) {
        smoothed_speed_bps_ = instantaneous_speed;
        initialized_ = true;
    } else {
        constexpr double smoothing_period_ms = 3000.0;
        const double alpha = -std::expm1(-static_cast<double>(delta_ms) / smoothing_period_ms);
        smoothed_speed_bps_ += alpha * (instantaneous_speed - smoothed_speed_bps_);
    }

    previous_bytes_ = bytes_transferred;
    previous_elapsed_ms_ = elapsed_ms;
    return bounded_speed(smoothed_speed_bps_);
}

} // namespace btrfsbackup
