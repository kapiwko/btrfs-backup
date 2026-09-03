// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/provisioning/BtrfsFilesystemFormatter.hpp>

#include <chrono>

#include <backup/ports/ICommandRunner.hpp>
#include <core/Errors.hpp>

namespace btrfsbackup::platform::linux::storage::provisioning {

CommandBtrfsFilesystemFormatter::CommandBtrfsFilesystemFormatter(backup::ICommandRunner& commands)
    : commands_(commands) {
}

void CommandBtrfsFilesystemFormatter::format(
    const std::filesystem::path& device,
    const std::string& label
) {
    if (!device.is_absolute() || device.lexically_normal() != device || label.empty())
        throw ValidationError("Btrfs format request is invalid");
    backup::ControlledCommandOptions options;
    options.timeout = std::chrono::minutes(10);
    const auto result = commands_.run_controlled(
        {"mkfs.btrfs", "--force", "--label", label, device.string()},
        options
    );
    if (result.exit_code != 0 || result.cancelled || result.timed_out)
        throw ValidationError("creating Btrfs filesystem failed");
}

} // namespace btrfsbackup::platform::linux::storage::provisioning
