// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/Process.hpp>

#include <core/Errors.hpp>
#include <platform/linux/ControlledCommandSession.hpp>

namespace btrfsbackup::platform::linux {

btrfsbackup::backup::CommandResult run_command(const std::vector<std::string>& argv) {
    btrfsbackup::backup::ControlledCommandOptions options;
    options.timeout = btrfsbackup::backup::default_command_timeout;
    options.max_output_bytes = btrfsbackup::backup::default_command_max_output_bytes;
    btrfsbackup::backup::CommandResult result = run_controlled_command(argv, options);
    if (result.timed_out) {
        result.exit_code = 124;
    }
    return result;
}

btrfsbackup::backup::CommandResult run_controlled_command(
    const std::vector<std::string>& argv,
    const btrfsbackup::backup::ControlledCommandOptions& options
) {
    ControlledCommandSession session(argv, options);
    return session.run();
}

std::string run_capture(const std::vector<std::string>& argv) {
    btrfsbackup::backup::CommandResult result = run_command(argv);
    if (result.exit_code != 0) {
        throw ValidationError("command failed: " + argv.front());
    }
    while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r')) {
        result.output.pop_back();
    }
    return result.output;
}

} // namespace btrfsbackup::platform::linux
