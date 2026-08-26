// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/posix_cancellation_signal.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include <core/errors.hpp>

namespace btrfsbackup::platform_linux {

void PosixCancellationSignal::WriteSignal::operator()() const noexcept {
    const char byte = 1;
    ssize_t result;
    do {
        result = write(fd, &byte, sizeof(byte));
    } while (result < 0 && errno == EINTR);
}

PosixCancellationSignal::PosixCancellationSignal(const CancellationToken& cancellation) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        throw ValidationError(std::string("cannot create cancellation pipe: ") + std::strerror(errno));
    }
    read_fd_ = fds[0];
    write_fd_ = fds[1];
    callback_.emplace(cancellation.stop_token(), WriteSignal{write_fd_});
}

PosixCancellationSignal::~PosixCancellationSignal() {
    callback_.reset();
    if (read_fd_ >= 0) {
        close(read_fd_);
    }
    if (write_fd_ >= 0) {
        close(write_fd_);
    }
}

int PosixCancellationSignal::fd() const noexcept {
    return read_fd_;
}

void PosixCancellationSignal::drain() const noexcept {
    char buffer[32];
    while (true) {
        const ssize_t count = read(read_fd_, buffer, sizeof(buffer));
        if (count > 0) {
            continue;
        }
        if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno != EINTR) {
            return;
        }
    }
}

} // namespace btrfsbackup::platform_linux
