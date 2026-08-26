// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <core/error_code.hpp>

#include "support/test_helpers.hpp"

namespace {

void test_error_code_names_are_stable_and_parseable() {
    using btrfsbackup::ErrorCode;
    test_helpers::expect_eq(
        "recovery error code",
        btrfsbackup::error_code_name(ErrorCode::RepositoryRecoveryRequired),
        "repository.recovery_required"
    );
    test_helpers::expect_true(
        "known error code",
        btrfsbackup::error_code_from_name("runner.profile_busy") == ErrorCode::RunnerProfileBusy,
        "known code was not parsed"
    );
    test_helpers::expect_true(
        "unknown error code",
        !btrfsbackup::error_code_from_name("future.unknown").has_value(),
        "unknown code should not be accepted"
    );
}

} // namespace

int main() {
    test_error_code_names_are_stable_and_parseable();
    return test_helpers::finish("error code tests");
}
