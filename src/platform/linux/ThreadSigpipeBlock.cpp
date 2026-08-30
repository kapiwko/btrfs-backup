// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/ThreadSigpipeBlock.hpp>

#include <pthread.h>

#include <cstring>
#include <string>

#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux {

namespace {

sigset_t sigpipe_set() noexcept {
    sigset_t signals{};
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    return signals;
}

} // namespace

ThreadSigpipeBlock::ThreadSigpipeBlock() {
    const sigset_t signals = sigpipe_set();
    const int result = pthread_sigmask(SIG_BLOCK, &signals, &previous_mask_);
    if (result != 0)
        throw ValidationError("cannot block SIGPIPE for transfer thread: " + std::string(std::strerror(result)));
    previously_blocked_ = sigismember(&previous_mask_, SIGPIPE) == 1;
}

ThreadSigpipeBlock::~ThreadSigpipeBlock() noexcept {
    if (!previously_blocked_) {
        const sigset_t signals = sigpipe_set();
        const timespec no_wait{};
        while (sigtimedwait(&signals, nullptr, &no_wait) == SIGPIPE) {
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
}

} // namespace btrfsbackup::platform::linux
