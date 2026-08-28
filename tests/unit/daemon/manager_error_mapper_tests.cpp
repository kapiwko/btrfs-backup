// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <stdexcept>
#include <string>

#include <core/errors.hpp>
#include <daemon/manager_error_mapper.hpp>

#include "support/test_helpers.hpp"

namespace {

using btrfsbackup::daemon::ManagerErrorCode;
using btrfsbackup::daemon::ManagerErrorMapper;

void expect_code(const std::string& name, const btrfsbackup::daemon::ManagerErrorDescription& error, ManagerErrorCode code) {
    test_helpers::expect_true(name, error.code == code, "unexpected manager error code");
}

void test_stable_error_catalog() {
    constexpr std::array codes{
        ManagerErrorCode::InvalidRequest,
        ManagerErrorCode::NotFound,
        ManagerErrorCode::NotAuthorized,
        ManagerErrorCode::Busy,
        ManagerErrorCode::RunMismatch,
        ManagerErrorCode::TargetUnavailable,
        ManagerErrorCode::Conflict,
        ManagerErrorCode::InternalError,
    };
    for (const auto code : codes) {
        const auto description = ManagerErrorMapper::describe(code);
        test_helpers::expect_contains("stable D-Bus namespace", description.dbus_name, "io.github.btrfsbackup.Error.");
        test_helpers::expect_true("public error message", std::string(description.public_message).size() > 0, "empty message");
    }
}

void test_exception_mapping() {
    const ManagerErrorMapper mapper;
    expect_code(
        "validation error",
        mapper.map(btrfsbackup::ValidationError("private validation detail")),
        ManagerErrorCode::InvalidRequest
    );
    expect_code(
        "busy error",
        mapper.map(btrfsbackup::CodedOperationError(btrfsbackup::ErrorCode::RunnerTargetBusy, "private target")),
        ManagerErrorCode::Busy
    );
    expect_code(
        "run mismatch error",
        mapper.map(btrfsbackup::CodedValidationError(btrfsbackup::ErrorCode::RunnerRunMismatch, "private run")),
        ManagerErrorCode::RunMismatch
    );
    expect_code(
        "conflict error",
        mapper.map(
            btrfsbackup::CodedOperationError(btrfsbackup::ErrorCode::TargetBtrfsUuidMismatch, "private UUID")
        ),
        ManagerErrorCode::Conflict
    );
    const auto internal = mapper.map(std::runtime_error("private internal detail"));
    expect_code("unexpected error", internal, ManagerErrorCode::InternalError);
    test_helpers::expect_true(
        "internal detail privacy",
        std::string(internal.public_message).find("private internal detail") == std::string::npos,
        "private exception message was exposed"
    );
}

} // namespace

int main() {
    test_stable_error_catalog();
    test_exception_mapping();
    return test_helpers::finish("manager error mapper tests");
}
