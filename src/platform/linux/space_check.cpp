// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/space_check.hpp>

#include <system_error>

#include <config/errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

unsigned long long available_bytes(const fs::path& path) {
    std::error_code ec;
    fs::space_info info = fs::space(path, ec);
    if (ec) {
        throw ValidationError("Could not determine free space at " + path.string());
    }
    return info.available;
}

void check_minimum_free_space(const fs::path& path, unsigned long long minimum_bytes, const std::string& label) {
    if (minimum_bytes == 0) {
        return;
    }
    unsigned long long available = available_bytes(path);
    if (available < minimum_bytes) {
        throw ValidationError(
            "Insufficient free space for " + label + " at " + path.string()
            + ": available=" + std::to_string(available)
            + ", required=" + std::to_string(minimum_bytes)
        );
    }
}

} // namespace btrfsbackup
