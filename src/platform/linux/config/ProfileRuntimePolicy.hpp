// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <config/domain/Profile.hpp>

namespace btrfsbackup::platform::linux::config {

inline constexpr const char* trusted_hook_directory = "/etc/btrfs-backup/hooks.d";

void validate_profile_runtime_policy(const btrfsbackup::config::Profile& profile);

} // namespace btrfsbackup::platform::linux::config
