// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace btrfsbackup::platform::linux {

class BoundedDiagnosticBuffer final {
  public:
    void append(std::string_view data);
    [[nodiscard]] std::string render() const;

  private:
    [[nodiscard]] std::string tail_text() const;

    static constexpr std::size_t segment_limit_bytes = 64U * 1024U;
    std::string head_;
    std::string tail_;
    std::size_t tail_start_ = 0;
    std::uint64_t discarded_bytes_ = 0;
};

} // namespace btrfsbackup::platform::linux
