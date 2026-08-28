// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <config/model/profile.hpp>

namespace btrfsbackup::backup {

class ITargetManager {
  public:
    virtual ~ITargetManager() = default;
    virtual void ensure_mounted(const btrfsbackup::config::Profile& profile) = 0;
};

} // namespace btrfsbackup::backup
