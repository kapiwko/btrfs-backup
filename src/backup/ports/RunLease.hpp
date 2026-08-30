// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <variant>

#include <config/model/Profile.hpp>
#include <core/ErrorCode.hpp>

namespace btrfsbackup::backup {

class IBackupRunLease {
  public:
    virtual ~IBackupRunLease() = default;
};

struct BackupRunLeaseAcquired {
    std::unique_ptr<IBackupRunLease> lease;
};

struct BackupRunLeaseBusy {
    ErrorCode error_code;
    std::string error_message;
};

using BackupRunLeaseResult = std::variant<BackupRunLeaseAcquired, BackupRunLeaseBusy>;

class IBackupRunLeaseProvider {
  public:
    virtual ~IBackupRunLeaseProvider() = default;

    [[nodiscard]] virtual BackupRunLeaseResult try_acquire(const btrfsbackup::config::Profile& profile) = 0;
};

} // namespace btrfsbackup::backup
