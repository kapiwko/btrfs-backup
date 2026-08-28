// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/system_run_context.hpp>

#include <chrono>
#include <string>

#include <core/runtime_time.hpp>

namespace btrfsbackup::backup {

RuntimeTimePoint SystemClock::now() const {
    return std::chrono::system_clock::now();
}

LocalDate SystemClock::local_date() const {
    return local_date_at(now());
}

RunId TimestampRunIdGenerator::generate(RuntimeTimePoint time) {
    const std::string snapshot_timestamp = format_utc_snapshot_timestamp(time);
    std::string compact;
    for (const char character : snapshot_timestamp) {
        if (character != '-' && character != ':') {
            compact.push_back(character);
        }
    }
    return RunId{compact + "-shadow"};
}

} // namespace btrfsbackup::backup
