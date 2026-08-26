// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <type_traits>

#include <core/errors.hpp>

static_assert(std::is_base_of_v<btrfsbackup::BtrfsBackupError, btrfsbackup::ConfigurationError>);
static_assert(std::is_base_of_v<btrfsbackup::ConfigurationError, btrfsbackup::ValidationError>);
static_assert(std::is_base_of_v<btrfsbackup::BtrfsBackupError, btrfsbackup::SystemOperationError>);
static_assert(std::is_base_of_v<btrfsbackup::SystemOperationError, btrfsbackup::CodedOperationError>);
static_assert(std::is_base_of_v<btrfsbackup::BtrfsBackupError, btrfsbackup::RecoveryRequiredError>);
static_assert(std::is_base_of_v<btrfsbackup::BtrfsBackupError, btrfsbackup::OperationCancelledError>);
static_assert(std::is_base_of_v<btrfsbackup::CodedError, btrfsbackup::CodedValidationError>);
static_assert(std::is_base_of_v<btrfsbackup::CodedError, btrfsbackup::CodedOperationError>);
static_assert(std::is_base_of_v<btrfsbackup::CodedError, btrfsbackup::RecoveryRequiredError>);

static_assert(!std::is_base_of_v<btrfsbackup::ValidationError, btrfsbackup::RecoveryRequiredError>);
static_assert(!std::is_base_of_v<btrfsbackup::ValidationError, btrfsbackup::OperationCancelledError>);

int main() {
    return 0;
}
