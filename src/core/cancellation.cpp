// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <core/cancellation.hpp>

namespace btrfsbackup {

void CancellationToken::request_cancel() {
    source_.request_stop();
}

bool CancellationToken::cancellation_requested() const noexcept {
    return source_.stop_requested();
}

std::stop_token CancellationToken::stop_token() const noexcept {
    return source_.get_token();
}

} // namespace btrfsbackup
