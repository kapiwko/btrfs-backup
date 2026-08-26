// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace btrfsbackup::platform_linux {

struct PosixTransferPumpResult {
    std::uint64_t bytes_transferred = 0;
    bool end_of_stream = false;
    bool would_block = false;
    std::string error;
};

PosixTransferPumpResult pump_posix_transfer(
    int producer_fd,
    int consumer_fd,
    std::size_t cycle_budget_bytes
);

} // namespace btrfsbackup::platform_linux
