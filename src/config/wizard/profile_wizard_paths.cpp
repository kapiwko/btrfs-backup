// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/wizard/profile_wizard_paths.hpp>

#include <unistd.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace btrfsbackup::config {

std::filesystem::path default_output_dir() {
    if (geteuid() == 0) {
        return "/etc/btrfs-backup/generated";
    }
    return fs::current_path() / "generated";
}

} // namespace btrfsbackup::config
