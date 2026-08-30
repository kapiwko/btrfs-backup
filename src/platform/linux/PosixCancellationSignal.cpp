// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/PosixCancellationSignal.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux {

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
    read_fd_.reset(fds[0]);
    write_fd_.reset(fds[1]);
    callback_.emplace(cancellation.stop_token(), WriteSignal{write_fd_.get()});
}

PosixCancellationSignal::~PosixCancellationSignal() = default;

int PosixCancellationSignal::fd() const noexcept {
    return read_fd_.get();
}

void PosixCancellationSignal::drain() const noexcept {
    char buffer[32];
    while (true) {
        const ssize_t count = read(read_fd_.get(), buffer, sizeof(buffer));
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

} // namespace btrfsbackup::platform::linux
