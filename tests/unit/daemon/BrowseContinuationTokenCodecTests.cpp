// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>
#include <string>

#include <daemon/control/BrowseContinuationTokenCodec.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include "support/TestHelpers.hpp"

namespace {
using btrfsbackup::BrowseSessionId;
using btrfsbackup::daemon::control::BrowseContinuationTokenCodec;
using btrfsbackup::daemon::dbus::ManagerErrorCode;
using btrfsbackup::daemon::dbus::ManagerOperationError;

void expect_invalid(const char* name, const std::function<void()>& operation) {
    try {
        operation();
        test_helpers::fail(name, "operation succeeded");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true(
            name,
            error.code() == ManagerErrorCode::InvalidRequest,
            "unexpected manager error"
        );
    }
}

void test_directory_tokens_are_bound_to_session_and_path() {
    const BrowseContinuationTokenCodec codec;
    const BrowseSessionId session{"browse-one"};
    const std::string token = codec.directory_token(session, "documents/./reports", "annual.txt");

    test_helpers::expect_eq(
        "directory token round trip",
        codec.directory_cursor(session, "documents/reports", token),
        "annual.txt"
    );
    expect_invalid("directory session binding", [&] {
        (void)codec.directory_cursor(BrowseSessionId{"browse-two"}, "documents/reports", token);
    });
    expect_invalid("directory path binding", [&] {
        (void)codec.directory_cursor(session, "documents/private", token);
    });
}

void test_directory_tokens_reject_invalid_input_and_cursor() {
    const BrowseContinuationTokenCodec codec;
    const BrowseSessionId session{"browse-one"};

    test_helpers::expect_true(
        "empty directory token",
        codec.directory_cursor(session, ".", "").empty(),
        "empty token did not select the first page"
    );
    expect_invalid("malformed version", [&] {
        (void)codec.directory_cursor(session, ".", "v2:00");
    });
    expect_invalid("malformed hexadecimal payload", [&] {
        (void)codec.directory_cursor(session, ".", "v1:zz");
    });
    expect_invalid("invalid directory cursor", [&] {
        const std::string token = codec.directory_token(session, ".", "nested/name");
        (void)codec.directory_cursor(session, ".", token);
    });
    expect_invalid("oversized generated token", [&] {
        (void)codec.directory_token(session, ".", std::string(32768, 'a'));
    });
    expect_invalid("oversized supplied token", [&] {
        (void)codec.directory_cursor(session, ".", std::string(32769, 'a'));
    });
}

void test_previous_versions_tokens_are_bound_to_the_complete_query() {
    const BrowseContinuationTokenCodec codec;
    const BrowseSessionId session{"browse-one"};
    const std::string token = codec.previous_versions_token(
        session,
        "default",
        "home",
        "documents/./file.txt",
        17
    );

    test_helpers::expect_true(
        "previous versions token round trip",
        codec.previous_versions_offset(session, "default", "home", "documents/file.txt", token) == 17,
        "previous versions offset changed"
    );
    test_helpers::expect_true(
        "empty previous versions token",
        codec.previous_versions_offset(session, "default", "home", "documents/file.txt", "") == 0,
        "empty token did not select the first page"
    );
    expect_invalid("previous versions session binding", [&] {
        (void)codec.previous_versions_offset(
            BrowseSessionId{"browse-two"},
            "default",
            "home",
            "documents/file.txt",
            token
        );
    });
    expect_invalid("previous versions profile binding", [&] {
        (void)codec.previous_versions_offset(session, "archive", "home", "documents/file.txt", token);
    });
    expect_invalid("previous versions source binding", [&] {
        (void)codec.previous_versions_offset(session, "default", "root", "documents/file.txt", token);
    });
    expect_invalid("previous versions path binding", [&] {
        (void)codec.previous_versions_offset(session, "default", "home", "other/file.txt", token);
    });
    expect_invalid("zero previous versions offset", [&] {
        (void)codec.previous_versions_token(session, "default", "home", "documents/file.txt", 0);
    });
}

} // namespace

int main() {
    test_directory_tokens_are_bound_to_session_and_path();
    test_directory_tokens_reject_invalid_input_and_cursor();
    test_previous_versions_tokens_are_bound_to_the_complete_query();
    return test_helpers::finish("browse continuation token codec tests");
}
