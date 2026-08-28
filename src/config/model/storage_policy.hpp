// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

namespace btrfsbackup::config {

class RetentionCount {
  public:
    static constexpr std::uint64_t maximum = 100000;

    explicit RetentionCount(std::uint64_t value);

    [[nodiscard]] std::size_t value() const noexcept;

    auto operator<=>(const RetentionCount&) const = default;

  private:
    std::size_t value_;
};

class ByteThreshold {
  public:
    static constexpr std::uint64_t maximum = 1000000000000000ULL;

    explicit ByteThreshold(std::uint64_t value);

    [[nodiscard]] std::uint64_t value() const noexcept;

    auto operator<=>(const ByteThreshold&) const = default;

  private:
    std::uint64_t value_;
};

} // namespace btrfsbackup::config
