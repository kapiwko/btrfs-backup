// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/ThreadSigpipeBlock.hpp>

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <future>
#include <thread>

#include "support/TestHelpers.hpp"

namespace {

bool sigpipe_blocked() {
    sigset_t current{};
    pthread_sigmask(SIG_SETMASK, nullptr, &current);
    return sigismember(&current, SIGPIPE) == 1;
}

void test_guard_is_thread_local() {
    const bool caller_initially_blocked = sigpipe_blocked();
    std::promise<void> guarded;
    std::promise<void> inspected;
    auto guarded_ready = guarded.get_future();
    auto inspected_ready = inspected.get_future();
    bool worker_blocked = true;

    std::thread worker([&] {
        guarded_ready.wait();
        worker_blocked = sigpipe_blocked();
        inspected.set_value();
    });
    {
        btrfsbackup::platform::linux::ThreadSigpipeBlock block;
        test_helpers::expect_true("guarded thread mask", sigpipe_blocked(), "SIGPIPE was not blocked");
        guarded.set_value();
        inspected_ready.wait();
    }
    worker.join();

    test_helpers::expect_true(
        "other thread mask",
        worker_blocked == caller_initially_blocked,
        "guard changed another thread's signal mask"
    );
    test_helpers::expect_true(
        "restored caller mask",
        sigpipe_blocked() == caller_initially_blocked,
        "guard did not restore the caller signal mask"
    );
}

void test_pending_sigpipe_is_consumed_before_unblocking() {
    test_helpers::expect_true("test precondition", !sigpipe_blocked(), "SIGPIPE unexpectedly blocked before test");
    {
        btrfsbackup::platform::linux::ThreadSigpipeBlock outer;
        {
            btrfsbackup::platform::linux::ThreadSigpipeBlock inner;
            int descriptors[2]{};
            test_helpers::expect_true("create pipe", pipe(descriptors) == 0, "pipe failed");
            close(descriptors[0]);
            const char value = 'x';
            test_helpers::expect_true("closed pipe write", write(descriptors[1], &value, 1) == -1, "write unexpectedly succeeded");
            close(descriptors[1]);
        }
        test_helpers::expect_true("nested guard mask", sigpipe_blocked(), "nested guard restored a stale mask");
    }
    test_helpers::expect_true("final restored mask", !sigpipe_blocked(), "SIGPIPE remained blocked");
}

} // namespace

int main() {
    test_guard_is_thread_local();
    test_pending_sigpipe_is_consumed_before_unblocking();
    return test_helpers::finish("thread SIGPIPE block tests");
}
