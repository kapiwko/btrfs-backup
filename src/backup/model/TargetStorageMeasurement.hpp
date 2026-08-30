// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include <core/RuntimeTime.hpp>

namespace btrfsbackup::backup {

struct FilesystemSpace {
    std::uint64_t capacity_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::uint64_t available_bytes = 0;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint64_t used_bytes() const noexcept;
    [[nodiscard]] int usage_percent() const noexcept;
};

struct TargetStorageMeasurement {
    FilesystemSpace space;
    RuntimeTimePoint measured_at;
};

} // namespace btrfsbackup::backup
