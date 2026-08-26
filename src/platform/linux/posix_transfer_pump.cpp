// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/posix_transfer_pump.hpp>

#include <cerrno>
#include <cstring>

#include <platform/linux/splice.hpp>

namespace btrfsbackup::platform_linux {

PosixTransferPumpResult pump_posix_transfer(
    int producer_fd,
    int consumer_fd,
    std::size_t cycle_budget_bytes
) {
    constexpr std::size_t splice_chunk_bytes = 1024U * 1024U;
    PosixTransferPumpResult result;
    while (result.bytes_transferred < cycle_budget_bytes) {
        const ssize_t count = splice_pipe(producer_fd, consumer_fd, splice_chunk_bytes);
        if (count > 0) {
            result.bytes_transferred += static_cast<std::uint64_t>(count);
            continue;
        }
        if (count == 0) {
            result.end_of_stream = true;
            return result;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            result.would_block = true;
            return result;
        }
        result.error = std::string("stream splice failed: ") + std::strerror(errno);
        return result;
    }
    result.would_block = true;
    return result;
}

} // namespace btrfsbackup::platform_linux
