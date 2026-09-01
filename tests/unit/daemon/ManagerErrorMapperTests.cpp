// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <stdexcept>
#include <string>

#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrorMapper.hpp>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::daemon::dbus::ManagerErrorCode;
using btrfsbackup::daemon::dbus::ManagerErrorMapper;

void expect_code(const std::string& name, const btrfsbackup::daemon::dbus::ManagerErrorDescription& error, ManagerErrorCode code) {
    test_helpers::expect_true(name, error.code == code, "unexpected manager error code");
}

void test_stable_error_catalog() {
    constexpr std::array codes{
        ManagerErrorCode::InvalidRequest,
        ManagerErrorCode::SourceMissing,
        ManagerErrorCode::SourceNotSubvolume,
        ManagerErrorCode::SourceUnavailable,
        ManagerErrorCode::NotFound,
        ManagerErrorCode::NotAuthorized,
        ManagerErrorCode::Busy,
        ManagerErrorCode::RunMismatch,
        ManagerErrorCode::TargetUnavailable,
        ManagerErrorCode::Conflict,
        ManagerErrorCode::SaveFailed,
        ManagerErrorCode::RollbackIncomplete,
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
    expect_code(
        "configuration changed error",
        mapper.map(btrfsbackup::CodedValidationError(btrfsbackup::ErrorCode::ConfigurationChanged, "private version")),
        ManagerErrorCode::Conflict
    );
    expect_code(
        "configuration save error",
        mapper.map(btrfsbackup::CodedValidationError(btrfsbackup::ErrorCode::ConfigurationSaveFailed, "private path")),
        ManagerErrorCode::SaveFailed
    );
    expect_code(
        "rollback error",
        mapper.map(btrfsbackup::CodedValidationError(btrfsbackup::ErrorCode::ConfigurationRollbackIncomplete, "private path")),
        ManagerErrorCode::RollbackIncomplete
    );
    expect_code(
        "credential rollback error",
        mapper.map(btrfsbackup::CodedOperationError(btrfsbackup::ErrorCode::CredentialMutationRollbackIncomplete, "private credential")),
        ManagerErrorCode::RollbackIncomplete
    );
    const auto internal = mapper.map(std::runtime_error("private internal detail"));
    expect_code("unexpected error", internal, ManagerErrorCode::InternalError);
    test_helpers::expect_true(
        "internal detail privacy",
        std::string(internal.public_message).find("private internal detail") == std::string::npos,
        "private exception message was exposed"
    );
    expect_code(
        "manager authorization error",
        mapper.map(btrfsbackup::daemon::dbus::ManagerOperationError(ManagerErrorCode::NotAuthorized, "private caller")),
        ManagerErrorCode::NotAuthorized
    );
}

} // namespace

int main() {
    test_stable_error_catalog();
    test_exception_mapping();
    return test_helpers::finish("manager error mapper tests");
}
