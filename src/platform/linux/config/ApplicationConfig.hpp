// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/ApplicationConfig.hpp>

namespace btrfsbackup::platform::linux::config {

[[nodiscard]] btrfsbackup::config::ApplicationConfig load_application_config(
    const std::filesystem::path& config_root = "/etc/btrfs-backup"
);

} // namespace btrfsbackup::platform::linux::config
