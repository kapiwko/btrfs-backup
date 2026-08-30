// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include <backup/BackupService.hpp>

namespace btrfsbackup::cli {

int runner(const std::filesystem::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output);
int runner(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    CancellationToken& cancellation
);
int runner(
    const std::vector<std::string>& args,
    std::ostream& output,
    btrfsbackup::backup::BackupService& service
);

} // namespace btrfsbackup::cli
