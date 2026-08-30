// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/DbusCallbackBoundary.hpp>

#include <cerrno>
#include <stdexcept>

#include "support/TestHelpers.hpp"

namespace {

void test_success_is_returned() {
    const int result = btrfsbackup::daemon::dbus::invoke_dbus_callback(
        [] { return 17; },
        [](const std::exception*) { return -1; }
    );
    test_helpers::expect_true("success", result == 17, "unexpected callback result");
}

void test_standard_exception_is_reported() {
    bool received_standard_exception = false;
    const int result = btrfsbackup::daemon::dbus::invoke_dbus_callback(
        []() -> int { throw std::runtime_error("failure"); },
        [&](const std::exception* exception) {
            received_standard_exception = exception != nullptr;
            return -23;
        }
    );
    test_helpers::expect_true("standard exception result", result == -23, "unexpected callback result");
    test_helpers::expect_true("standard exception identity", received_standard_exception, "exception was discarded");
}

void test_unknown_exception_is_contained() {
    bool received_unknown_exception = false;
    const int result = btrfsbackup::daemon::dbus::invoke_dbus_callback(
        []() -> int { throw 42; },
        [&](const std::exception* exception) {
            received_unknown_exception = exception == nullptr;
            return -5;
        }
    );
    test_helpers::expect_true("unknown exception result", result == -5, "unexpected callback result");
    test_helpers::expect_true("unknown exception identity", received_unknown_exception, "unknown exception was misclassified");
}

void test_error_handler_failure_is_contained() {
    const int result = btrfsbackup::daemon::dbus::invoke_dbus_callback(
        []() -> int { throw std::runtime_error("failure"); },
        [](const std::exception*) -> int { throw 42; }
    );
    test_helpers::expect_true("error handler failure", result == -EIO, "unexpected callback result");
}

} // namespace

int main() {
    test_success_is_returned();
    test_standard_exception_is_reported();
    test_unknown_exception_is_contained();
    test_error_handler_failure_is_contained();
    return test_helpers::finish("D-Bus callback boundary tests");
}
