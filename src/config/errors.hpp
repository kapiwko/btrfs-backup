// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdexcept>
#include <string>
#include <utility>

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
    explicit CodedError(std::string error_code) : error_code(std::move(error_code)) {
    }

    virtual ~CodedError() = default;

    std::string error_code;
};

struct CodedValidationError : ValidationError, CodedError {
    CodedValidationError(std::string error_code, const std::string& message)
        : ValidationError(message), CodedError(std::move(error_code)) {
    }
};

struct CodedOperationError : SystemOperationError, CodedError {
    CodedOperationError(std::string error_code, const std::string& message)
        : SystemOperationError(message), CodedError(std::move(error_code)) {
    }
};

struct RecoveryRequiredError : BtrfsBackupError, CodedError {
    RecoveryRequiredError(std::string error_code, const std::string& message)
        : BtrfsBackupError(message), CodedError(std::move(error_code)) {
    }
};

struct OperationCancelledError : BtrfsBackupError {
    using BtrfsBackupError::BtrfsBackupError;
};

} // namespace btrfsbackup
