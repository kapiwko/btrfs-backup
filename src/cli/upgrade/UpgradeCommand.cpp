// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/upgrade/UpgradeCommand.hpp>

#include <ostream>
#include <string>
#include <vector>

#include <platform/linux/config/ProfileMigration.hpp>

namespace btrfsbackup::cli::upgrade {

namespace {

void usage(std::ostream& output) {
    output << "Usage: btrfs-backupctl [--profile-dir PATH] upgrade preflight\n";
}

} // namespace

int upgrade(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    std::ostream& error
) {
    if (args.size() == 1 && (args[0] == "-h" || args[0] == "--help")) {
        usage(output);
        return 0;
    }
    if (args.size() != 1 || args[0] != "preflight") {
        error << "btrfs-backupctl upgrade: expected preflight\n";
        usage(error);
        return 2;
    }

    const auto result = btrfsbackup::platform::linux::config::inspect_profile_migration_readiness(
        profile_config_dir
    );
    for (const std::string& id : result.ready_profiles) {
        output << "READY " << id << ": schema version 4 with configuration generation\n";
    }
    for (const auto& issue : result.issues) {
        error << "BLOCKED " << issue.profile_id << ": " << issue.message << '\n';
    }
    if (!result.issues.empty()) {
        error << "Export a complete backup before upgrading:\n"
              << "  btrfs-backupctl profile export-v4 --all --output-dir PATH\n";
        return 1;
    }
    output << "READY: all " << result.ready_profiles.size() << " profile(s) are compatible with 4.0\n";
    output << "Create a complete backup before upgrading:\n"
           << "  btrfs-backupctl profile export-v4 --all --output-dir PATH\n";
    return 0;
}

} // namespace btrfsbackup::cli::upgrade
