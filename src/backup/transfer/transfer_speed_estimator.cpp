// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/transfer/transfer_speed_estimator.hpp>

#include <cmath>
#include <limits>

namespace btrfsbackup {

std::uint64_t TransferSpeedEstimator::sample(
    std::uint64_t bytes_transferred,
    std::chrono::milliseconds elapsed
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
    if (elapsed <= previous_elapsed_) {
        return bounded_speed(smoothed_speed_bps_);
    }

    const std::uint64_t delta_bytes = bytes_transferred >= previous_bytes_
        ? bytes_transferred - previous_bytes_
        : bytes_transferred;
    const std::chrono::milliseconds delta = elapsed - previous_elapsed_;
    const double delta_seconds = std::chrono::duration<double>(delta).count();
    const double instantaneous_speed = static_cast<double>(delta_bytes) / delta_seconds;
    if (!initialized_) {
        smoothed_speed_bps_ = instantaneous_speed;
        initialized_ = true;
    } else {
        constexpr std::chrono::seconds smoothing_period{3};
        const double smoothing_seconds = std::chrono::duration<double>(smoothing_period).count();
        const double alpha = -std::expm1(-delta_seconds / smoothing_seconds);
        smoothed_speed_bps_ += alpha * (instantaneous_speed - smoothed_speed_bps_);
    }

    previous_bytes_ = bytes_transferred;
    previous_elapsed_ = elapsed;
    return bounded_speed(smoothed_speed_bps_);
}

} // namespace btrfsbackup
