// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/posix_command_runner.hpp>

namespace btrfsbackup {

CommandResult PosixCommandRunner::run(const std::vector<std::string>& argv) {
    return run_command(argv);
}

CommandResult PosixCommandRunner::run_controlled(
    const std::vector<std::string>& argv,
    const ControlledCommandOptions& options
) {
    return run_controlled_command(argv, options);
}

} // namespace btrfsbackup
