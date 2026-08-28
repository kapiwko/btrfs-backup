// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace btrfsbackup {

using RuntimeTimePoint = std::chrono::system_clock::time_point;
using LocalDate = std::chrono::year_month_day;

[[nodiscard]] std::string format_utc_snapshot_timestamp(RuntimeTimePoint time);
[[nodiscard]] std::string format_utc_iso_timestamp(RuntimeTimePoint time);
[[nodiscard]] std::string format_local_date(LocalDate date);
[[nodiscard]] std::string format_local_timestamp(RuntimeTimePoint time);
[[nodiscard]] LocalDate local_date_at(RuntimeTimePoint time);
[[nodiscard]] std::optional<RuntimeTimePoint> parse_utc_timestamp(const std::string& value);
[[nodiscard]] std::optional<LocalDate> parse_local_date(const std::string& value);

} // namespace btrfsbackup
