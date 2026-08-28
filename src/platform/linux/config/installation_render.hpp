// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/model/profile.hpp>

namespace btrfsbackup::platform::linux {

// Linux installation artifact rendering contract.

struct InstallationRenderOptions {
    std::string backup_command = "/usr/bin/btrfs-backupctl runner execute";
    std::string eject_script = "/usr/bin/btrfs-backupctl target eject";
    std::string keyfile = "none";
};

void render_installation_files(
    const btrfsbackup::config::Profile& profile,
    const std::filesystem::path& output_dir,
    const InstallationRenderOptions& options
);

} // namespace btrfsbackup::platform::linux
