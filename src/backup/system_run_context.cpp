// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/system_run_context.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace btrfsbackup::backup {

namespace {

std::string format_time(const char* format, bool utc) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    if (utc) {
        gmtime_r(&time, &tm);
    } else {
        localtime_r(&time, &tm);
    }
    std::ostringstream out;
    out << std::put_time(&tm, format);
    return out.str();
}

} // namespace

std::string SystemClock::snapshot_timestamp() const {
    return format_time("%Y-%m-%dT%H%M%SZ", true);
}

std::string SystemClock::local_date() const {
    return format_time("%Y-%m-%d", false);
}

std::string SystemClock::local_timestamp() const {
    return format_time("%Y-%m-%dT%H:%M:%S%z", false);
}

RunId TimestampRunIdGenerator::generate(const std::string& snapshot_timestamp) {
    std::string compact;
    for (const char character : snapshot_timestamp) {
        if (character != '-' && character != ':') {
            compact.push_back(character);
        }
    }
    return RunId{compact + "-shadow"};
}

} // namespace btrfsbackup::backup
