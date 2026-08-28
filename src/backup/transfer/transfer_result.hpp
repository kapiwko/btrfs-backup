// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <core/error_code.hpp>

namespace btrfsbackup::backup::transfer {

struct TransferSideResult {
    bool started = false;
    int exit_code = -1;
    std::string diagnostics;
};

struct TransferResult {
    TransferSideResult producer;
    TransferSideResult consumer;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t bytes_produced = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t duration_ms = 0;
    std::uint64_t average_speed_bps = 0;
    bool cancelled = false;
};

[[nodiscard]] bool transfer_succeeded(const TransferResult& result);
[[nodiscard]] std::optional<ErrorCode> transfer_failure_error_code(const TransferResult& result);
void require_transfer_success(const TransferResult& result);

} // namespace btrfsbackup::backup::transfer
