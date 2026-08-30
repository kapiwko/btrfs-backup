// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/TransferProgressReporter.hpp>

#include <limits>

namespace btrfsbackup::platform::linux {

std::uint64_t transfer_elapsed_ms(TransferSteadyClock::time_point started_at) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(TransferSteadyClock::now() - started_at).count()
    );
}

std::uint64_t transfer_average_speed_bps(std::uint64_t bytes, std::uint64_t elapsed_ms) {
    if (elapsed_ms == 0)
        return 0;
    const long double rate = static_cast<long double>(bytes) * 1000.0L / static_cast<long double>(elapsed_ms);
    return rate >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(rate);
}

void emit_transfer_event(
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    btrfsbackup::backup::transfer::TransferEventKind kind,
    const btrfsbackup::backup::transfer::TransferResult& result,
    TransferSteadyClock::time_point started_at,
    std::uint64_t delta_bytes,
    const std::string& message,
    std::optional<std::uint64_t> reported_speed_bps
) {
    const std::uint64_t elapsed = transfer_elapsed_ms(started_at);
    events.on_transfer_event({
        .kind = kind,
        .bytes_transferred = result.bytes_transferred,
        .bytes_produced = result.bytes_produced,
        .bytes_total_estimated = result.bytes_total_estimated,
        .delta_bytes = delta_bytes,
        .elapsed_ms = elapsed,
        .speed_bps = reported_speed_bps.value_or(transfer_average_speed_bps(result.bytes_transferred, elapsed)),
        .message = message,
    });
}

TransferProgressReporter::TransferProgressReporter(TransferSteadyClock::time_point started_at)
    : started_at_(started_at), last_report_at_(started_at) {
}

void TransferProgressReporter::maybe_report(
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    const btrfsbackup::backup::transfer::TransferResult& result
) {
    const TransferSteadyClock::time_point now = TransferSteadyClock::now();
    if (now - last_report_at_ >= report_interval_)
        report(events, result, now);
}

void TransferProgressReporter::flush(
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    const btrfsbackup::backup::transfer::TransferResult& result
) {
    if (!reported_ || result.bytes_transferred != last_reported_bytes_)
        report(events, result, TransferSteadyClock::now());
}

void TransferProgressReporter::report(
    btrfsbackup::backup::transfer::ITransferEventSink& events,
    const btrfsbackup::backup::transfer::TransferResult& result,
    TransferSteadyClock::time_point now
) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at_);
    const std::uint64_t delta_bytes = result.bytes_transferred >= last_reported_bytes_
        ? result.bytes_transferred - last_reported_bytes_
        : 0;
    emit_transfer_event(
        events,
        btrfsbackup::backup::transfer::TransferEventKind::Progress,
        result,
        started_at_,
        delta_bytes,
        "",
        speed_.sample(result.bytes_transferred, elapsed)
    );
    last_report_at_ = now;
    last_reported_bytes_ = result.bytes_transferred;
    reported_ = true;
}

} // namespace btrfsbackup::platform::linux
