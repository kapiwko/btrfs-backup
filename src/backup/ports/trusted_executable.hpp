// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace btrfsbackup {

struct TrustedExecutablePolicy {
    bool allow_current_user_owner = false;
    bool verify_parent_directories = true;
};

} // namespace btrfsbackup
