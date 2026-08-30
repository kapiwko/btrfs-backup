// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <core/ErrorCode.hpp>

namespace btrfsbackup::backup::transfer {

struct TransferNotStarted {
    std::string diagnostics;
};

struct TransferRunning {
    std::string diagnostics;
};

struct TransferExited {
    int exit_code;
    std::string diagnostics;
};

class TransferSideResult {
  public:
    TransferSideResult() = default;
    static TransferSideResult not_started(std::string diagnostics = {});
    static TransferSideResult running();
    static TransferSideResult exited(int exit_code, std::string diagnostics = {});

    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] std::optional<int> exit_code() const noexcept;
    [[nodiscard]] const std::string& diagnostics() const noexcept;
    [[nodiscard]] std::string& diagnostics() noexcept;
    void mark_exited(int exit_code);

  private:
    explicit TransferSideResult(std::variant<TransferNotStarted, TransferRunning, TransferExited> state);

    std::variant<TransferNotStarted, TransferRunning, TransferExited> state_{TransferNotStarted{}};
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
