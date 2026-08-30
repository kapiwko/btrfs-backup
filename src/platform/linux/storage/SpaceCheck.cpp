// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/SpaceCheck.hpp>

#include <system_error>

#include <core/Errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux::storage {

std::uint64_t available_bytes(const fs::path& path) {
    std::error_code ec;
    fs::space_info info = fs::space(path, ec);
    if (ec) {
        throw ValidationError("Could not determine free space at " + path.string());
    }
    return info.available;
}

void check_minimum_free_space(const fs::path& path, std::uint64_t minimum_bytes, const std::string& label) {
    if (minimum_bytes == 0) {
        return;
    }
    const std::uint64_t available = available_bytes(path);
    if (available < minimum_bytes) {
        throw ValidationError(
            "Insufficient free space for " + label + " at " + path.string() + ": available=" + std::to_string(available) + ", required=" + std::to_string(minimum_bytes)
        );
    }
}

} // namespace btrfsbackup::platform::linux::storage
