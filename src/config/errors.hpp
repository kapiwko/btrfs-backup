// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace btrfsbackup {

struct ValidationError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct CodedValidationError : ValidationError {
    CodedValidationError(std::string error_code, const std::string& message)
        : ValidationError(message), error_code(std::move(error_code)) {
    }

    std::string error_code;
};

struct RecoveryRequiredError : CodedValidationError {
    using CodedValidationError::CodedValidationError;
};

struct OperationCancelledError : ValidationError {
    using ValidationError::ValidationError;
};

} // namespace btrfsbackup
