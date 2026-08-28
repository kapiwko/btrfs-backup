// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdexcept>
#include <string>

#include <core/error_code.hpp>

namespace btrfsbackup {

struct BtrfsBackupError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ConfigurationError : BtrfsBackupError {
    using BtrfsBackupError::BtrfsBackupError;
};

struct ValidationError : ConfigurationError {
    using ConfigurationError::ConfigurationError;
};

struct SystemOperationError : BtrfsBackupError {
    using BtrfsBackupError::BtrfsBackupError;
};

struct CodedError {
    explicit CodedError(ErrorCode error_code) : error_code(error_code) {
    }

    virtual ~CodedError() = default;

    ErrorCode error_code;
};

struct CodedValidationError : ValidationError, CodedError {
    CodedValidationError(ErrorCode error_code, const std::string& message)
        : ValidationError(message), CodedError(error_code) {
    }
};

struct CodedOperationError : SystemOperationError, CodedError {
    CodedOperationError(ErrorCode error_code, const std::string& message)
        : SystemOperationError(message), CodedError(error_code) {
    }
};

struct RecoveryRequiredError : BtrfsBackupError, CodedError {
    RecoveryRequiredError(ErrorCode error_code, const std::string& message)
        : BtrfsBackupError(message), CodedError(error_code) {
    }
};

struct OperationCancelledError : BtrfsBackupError {
    using BtrfsBackupError::BtrfsBackupError;
};

} // namespace btrfsbackup
