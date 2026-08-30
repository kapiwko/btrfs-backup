// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <backup/transfer/ITransferPipeline.hpp>
#include <backup/transfer/TransferSpeedEstimator.hpp>

namespace btrfsbackup::platform::linux {

using TransferSteadyClock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t transfer_elapsed_ms(TransferSteadyClock::time_point started_at);
[[nodiscard]] std::uint64_t transfer_average_speed_bps(std::uint64_t bytes, std::uint64_t elapsed_ms);
void emit_transfer_event(
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    btrfsbackup::backup::transfer::TransferEventKind kind,
    const btrfsbackup::backup::transfer::TransferResult& result,
    TransferSteadyClock::time_point started_at,
    std::uint64_t delta_bytes = 0,
    const std::string& message = "",
    std::optional<std::uint64_t> reported_speed_bps = std::nullopt
);

class TransferProgressReporter final {
  public:
    explicit TransferProgressReporter(TransferSteadyClock::time_point started_at);
    void maybe_report(
        btrfsbackup::backup::transfer::ITransferEventSink& events,
        const btrfsbackup::backup::transfer::TransferResult& result
    );
    void flush(
        btrfsbackup::backup::transfer::ITransferEventSink& events,
        const btrfsbackup::backup::transfer::TransferResult& result
    );

  private:
    void report(
        btrfsbackup::backup::transfer::ITransferEventSink& events,
        const btrfsbackup::backup::transfer::TransferResult& result,
        TransferSteadyClock::time_point now
    );

    static constexpr std::chrono::milliseconds report_interval_{500};
    TransferSteadyClock::time_point started_at_;
    TransferSteadyClock::time_point last_report_at_;
    btrfsbackup::backup::transfer::TransferSpeedEstimator speed_;
    std::uint64_t last_reported_bytes_ = 0;
    bool reported_ = false;
};

} // namespace btrfsbackup::platform::linux
