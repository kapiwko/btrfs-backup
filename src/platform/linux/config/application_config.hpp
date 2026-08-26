// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/application_config.hpp>

namespace btrfsbackup {

[[nodiscard]] ApplicationConfig load_application_config(
    const std::filesystem::path& config_root = "/etc/btrfs-backup"
);

} // namespace btrfsbackup
