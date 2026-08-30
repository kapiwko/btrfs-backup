// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/model/TargetStorageMeasurement.hpp>

#include <algorithm>

namespace btrfsbackup::backup {

bool FilesystemSpace::valid() const noexcept {
    return free_bytes <= capacity_bytes && available_bytes <= free_bytes;
}

std::uint64_t FilesystemSpace::used_bytes() const noexcept {
    return valid() ? capacity_bytes - free_bytes : 0;
}

int FilesystemSpace::usage_percent() const noexcept {
    if (!valid()) {
        return 0;
    }
    const std::uint64_t used = used_bytes();
    const std::uint64_t usable_total = used + available_bytes;
    if (usable_total == 0 || used == 0) {
        return 0;
    }

    const std::uint64_t whole_percent = usable_total / 100;
    const std::uint64_t remainder = usable_total % 100;
    for (int percent = 1; percent <= 100; ++percent) {
        const auto value = static_cast<std::uint64_t>(percent);
        const std::uint64_t threshold = whole_percent * value + remainder * value / 100;
        if (used <= threshold) {
            return percent;
        }
    }
    return 100;
}

} // namespace btrfsbackup::backup
