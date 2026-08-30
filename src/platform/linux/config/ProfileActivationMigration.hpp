// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/model/Profile.hpp>

namespace btrfsbackup::platform::linux {

[[nodiscard]] btrfsbackup::config::Profile migrate_target_activation_from_crypttab(
    btrfsbackup::config::Profile profile,
    const std::filesystem::path& crypttab_path
);

} // namespace btrfsbackup::platform::linux
