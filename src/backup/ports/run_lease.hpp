// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <config/model/profile.hpp>
#include <core/error_code.hpp>

namespace btrfsbackup {

class IBackupRunLease {
  public:
    virtual ~IBackupRunLease() = default;
};

struct BackupRunLeaseResult {
    std::unique_ptr<IBackupRunLease> lease;
    std::optional<ErrorCode> error_code;
    std::string error_message;
};

class IBackupRunLeaseProvider {
  public:
    virtual ~IBackupRunLeaseProvider() = default;

    [[nodiscard]] virtual BackupRunLeaseResult try_acquire(const Profile& profile) = 0;
};

} // namespace btrfsbackup
