// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/model/profile.hpp>

namespace btrfsbackup::platform::linux {

// Linux installation artifact rendering contract.

[[nodiscard]] std::string default_backup_command();
[[nodiscard]] std::string default_eject_script();

struct InstallationRenderOptions {
    std::string backup_command = default_backup_command();
    std::string eject_script = default_eject_script();
    std::string keyfile = "none";
};

void render_installation_files(
    const btrfsbackup::config::Profile& profile,
    const std::filesystem::path& output_dir,
    const InstallationRenderOptions& options
);

} // namespace btrfsbackup::platform::linux
