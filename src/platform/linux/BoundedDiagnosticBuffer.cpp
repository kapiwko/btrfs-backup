// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/BoundedDiagnosticBuffer.hpp>

#include <algorithm>
#include <cstring>

namespace btrfsbackup::platform::linux {

void BoundedDiagnosticBuffer::append(std::string_view data) {
    const std::size_t head_remaining = segment_limit_bytes - head_.size();
    const std::size_t head_bytes = std::min(head_remaining, data.size());
    head_.append(data.data(), head_bytes);
    data.remove_prefix(head_bytes);
    if (data.empty())
        return;

    if (data.size() >= segment_limit_bytes) {
        discarded_bytes_ += tail_.size() + data.size() - segment_limit_bytes;
        tail_.assign(data.substr(data.size() - segment_limit_bytes));
        tail_start_ = 0;
        return;
    }

    const std::size_t tail_remaining = segment_limit_bytes - tail_.size();
    const std::size_t appended_bytes = std::min(tail_remaining, data.size());
    tail_.append(data.data(), appended_bytes);
    data.remove_prefix(appended_bytes);
    if (data.empty())
        return;

    discarded_bytes_ += data.size();
    const std::size_t first_part = std::min(data.size(), segment_limit_bytes - tail_start_);
    std::memcpy(tail_.data() + tail_start_, data.data(), first_part);
    std::memcpy(tail_.data(), data.data() + first_part, data.size() - first_part);
    tail_start_ = (tail_start_ + data.size()) % segment_limit_bytes;
}

std::string BoundedDiagnosticBuffer::render() const {
    if (discarded_bytes_ == 0)
        return head_ + tail_;
    return head_ + "\n... omitted " + std::to_string(discarded_bytes_) +
        " diagnostic bytes ...\n" + tail_text();
}

std::string BoundedDiagnosticBuffer::tail_text() const {
    if (tail_start_ == 0)
        return tail_;
    return tail_.substr(tail_start_) + tail_.substr(0, tail_start_);
}

} // namespace btrfsbackup::platform::linux
