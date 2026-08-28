// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <config/model/profile.hpp>

namespace btrfsbackup::backup {

class IBackupPreflight {
  public:
    virtual ~IBackupPreflight() = default;

    virtual void run(const btrfsbackup::config::Profile& profile) = 0;
};

} // namespace btrfsbackup::backup
