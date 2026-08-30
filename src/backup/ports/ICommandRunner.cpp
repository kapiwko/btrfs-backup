// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/ports/ICommandRunner.hpp>

#include <core/Errors.hpp>

namespace btrfsbackup::backup {

std::string capture_command(ICommandRunner& runner, const std::vector<std::string>& argv) {
    if (argv.empty()) {
        throw ValidationError("empty command");
    }
    CommandResult result = runner.run(argv);
    if (result.exit_code != 0) {
        throw ValidationError("command failed: " + argv.front());
    }
    while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r')) {
        result.output.pop_back();
    }
    return result.output;
}

} // namespace btrfsbackup::backup
