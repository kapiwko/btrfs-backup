// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <core/runtime_time.hpp>

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace btrfsbackup {

namespace {

std::optional<int> decimal(std::string_view value) {
    int result = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        result = result * 10 + (character - '0');
    }
    return result;
}

std::string format_utc(RuntimeTimePoint time, bool compact_clock) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(time);
    const auto day = std::chrono::floor<std::chrono::days>(seconds);
    const std::chrono::year_month_day date{day};
    const std::chrono::hh_mm_ss clock{seconds - day};
    std::ostringstream output;
    output << std::setfill('0')
           << std::setw(4) << static_cast<int>(date.year()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.month()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.day()) << 'T'
           << std::setw(2) << clock.hours().count();
    if (!compact_clock) {
        output << ':';
    }
    output << std::setw(2) << clock.minutes().count();
    if (!compact_clock) {
        output << ':';
    }
    output << std::setw(2) << clock.seconds().count() << 'Z';
    return output.str();
}

} // namespace

std::string format_utc_snapshot_timestamp(RuntimeTimePoint time) {
    return format_utc(time, true);
}

std::string format_utc_iso_timestamp(RuntimeTimePoint time) {
    return format_utc(time, false);
}

std::string format_local_date(LocalDate date) {
    std::ostringstream output;
    output << std::setfill('0')
           << std::setw(4) << static_cast<int>(date.year()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.month()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.day());
    return output.str();
}

std::string format_local_timestamp(RuntimeTimePoint time) {
    const std::time_t epoch_time = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
    localtime_r(&epoch_time, &local);
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%dT%H:%M:%S%z");
    return output.str();
}

LocalDate local_date_at(RuntimeTimePoint time) {
    const std::time_t epoch_time = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
    localtime_r(&epoch_time, &local);
    return LocalDate{
        std::chrono::year{local.tm_year + 1900},
        std::chrono::month{static_cast<unsigned>(local.tm_mon + 1)},
        std::chrono::day{static_cast<unsigned>(local.tm_mday)},
    };
}

std::optional<RuntimeTimePoint> parse_utc_timestamp(const std::string& value) {
    const bool compact = value.size() == 18;
    const bool iso = value.size() == 20;
    if ((!compact && !iso) || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value.back() != 'Z') {
        return std::nullopt;
    }
    if (iso && (value[13] != ':' || value[16] != ':')) {
        return std::nullopt;
    }

    const auto year = decimal(std::string_view(value).substr(0, 4));
    const auto month = decimal(std::string_view(value).substr(5, 2));
    const auto day = decimal(std::string_view(value).substr(8, 2));
    const auto hour = decimal(std::string_view(value).substr(11, 2));
    const std::size_t minute_offset = iso ? 14 : 13;
    const std::size_t second_offset = iso ? 17 : 15;
    const auto minute = decimal(std::string_view(value).substr(minute_offset, 2));
    const auto second = decimal(std::string_view(value).substr(second_offset, 2));
    if (!year || !month || !day || !hour || !minute || !second) {
        return std::nullopt;
    }
    const LocalDate date{
        std::chrono::year{*year},
        std::chrono::month{static_cast<unsigned>(*month)},
        std::chrono::day{static_cast<unsigned>(*day)},
    };
    if (!date.ok() || *hour > 23 || *minute > 59 || *second > 59) {
        return std::nullopt;
    }
    return RuntimeTimePoint{std::chrono::sys_days{date}.time_since_epoch()} + std::chrono::hours{*hour} + std::chrono::minutes{*minute} + std::chrono::seconds{*second};
}

std::optional<LocalDate> parse_local_date(const std::string& value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return std::nullopt;
    }
    const auto year = decimal(std::string_view(value).substr(0, 4));
    const auto month = decimal(std::string_view(value).substr(5, 2));
    const auto day = decimal(std::string_view(value).substr(8, 2));
    if (!year || !month || !day) {
        return std::nullopt;
    }
    const LocalDate date{
        std::chrono::year{*year},
        std::chrono::month{static_cast<unsigned>(*month)},
        std::chrono::day{static_cast<unsigned>(*day)},
    };
    return date.ok() ? std::optional<LocalDate>{date} : std::nullopt;
}

} // namespace btrfsbackup
