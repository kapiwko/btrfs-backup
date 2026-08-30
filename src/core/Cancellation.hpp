// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stop_token>

namespace btrfsbackup {

class CancellationToken {
  public:
    CancellationToken() = default;
    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;

    void request_cancel();
    [[nodiscard]] bool cancellation_requested() const noexcept;
    [[nodiscard]] std::stop_token stop_token() const noexcept;

  private:
    std::stop_source source_;
};

} // namespace btrfsbackup
