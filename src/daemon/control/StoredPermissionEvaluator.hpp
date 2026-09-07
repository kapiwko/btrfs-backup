// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <daemon/control/BrowseSessionService.hpp>

namespace btrfsbackup::daemon::control {

class StoredPermissionEvaluator final {
  public:
    static void require_access(
        int descriptor,
        const BrowseAccessIdentity* identity,
        int required,
        const char* operation
    );
};

} // namespace btrfsbackup::daemon::control
