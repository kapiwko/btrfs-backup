// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/splice.hpp>

#include <fcntl.h>

namespace btrfsbackup::platform::linux {

ssize_t splice_pipe(int source_fd, int target_fd, std::size_t max_bytes) {
    return ::splice(
        source_fd,
        nullptr,
        target_fd,
        nullptr,
        max_bytes,
        SPLICE_F_NONBLOCK | SPLICE_F_MORE
    );
}

} // namespace btrfsbackup::platform::linux
