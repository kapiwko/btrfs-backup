// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/posix_command_runner.hpp>

namespace btrfsbackup::platform::linux {

btrfsbackup::backup::CommandResult PosixCommandRunner::run(const std::vector<std::string>& argv) {
    return run_command(argv);
}

btrfsbackup::backup::CommandResult PosixCommandRunner::run_controlled(
    const std::vector<std::string>& argv,
    const btrfsbackup::backup::ControlledCommandOptions& options
) {
    return run_controlled_command(argv, options);
}

} // namespace btrfsbackup::platform::linux
