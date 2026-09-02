// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/storage/BtrfsFilesystemFormatter.hpp>

#include <chrono>
#include <vector>

#include <backup/ports/ICommandRunner.hpp>
#include <core/Errors.hpp>

#include "support/TestHelpers.hpp"

namespace {

class Commands final : public btrfsbackup::backup::ICommandRunner {
  public:
    btrfsbackup::backup::CommandResult result;
    std::vector<std::string> argv;
    btrfsbackup::backup::ControlledCommandOptions options;
    int calls = 0;

    btrfsbackup::backup::CommandResult run(const std::vector<std::string>&) override {
        test_helpers::fail("uncontrolled formatter command", "run() was called");
        return {};
    }

    btrfsbackup::backup::CommandResult run_controlled(
        const std::vector<std::string>& command,
        const btrfsbackup::backup::ControlledCommandOptions& command_options
    ) override {
        ++calls;
        argv = command;
        options = command_options;
        return result;
    }
};

void test_uses_fixed_controlled_command() {
    Commands commands;
    btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter formatter(commands);
    formatter.format("/dev/mapper/backup", "Backup target");

    test_helpers::expect_true(
        "formatter argv",
        commands.argv == std::vector<std::string>{
                             "mkfs.btrfs",
                             "--force",
                             "--label",
                             "Backup target",
                             "/dev/mapper/backup",
                         },
        "unexpected mkfs.btrfs arguments"
    );
    test_helpers::expect_true(
        "formatter timeout",
        commands.options.timeout == std::chrono::minutes(10),
        "mkfs.btrfs timeout is not ten minutes"
    );
}

void test_rejects_invalid_request_before_execution() {
    Commands commands;
    btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter formatter(commands);

    try {
        formatter.format("dev/mapper/backup", "Backup target");
        test_helpers::fail("relative formatter target", "relative device path was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
    try {
        formatter.format("/dev/mapper/backup", "");
        test_helpers::fail("empty formatter label", "empty label was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
    test_helpers::expect_true(
        "invalid formatter request has no side effects",
        commands.calls == 0,
        "mkfs.btrfs was invoked for an invalid request"
    );
}

void test_rejects_failed_or_interrupted_command() {
    for (const auto result : {
             btrfsbackup::backup::CommandResult{.exit_code = 1},
             btrfsbackup::backup::CommandResult{.cancelled = true},
             btrfsbackup::backup::CommandResult{.timed_out = true},
         }) {
        Commands commands;
        commands.result = result;
        btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter formatter(commands);
        try {
            formatter.format("/dev/mapper/backup", "Backup target");
            test_helpers::fail("failed formatter command", "failed command was accepted");
        } catch (const btrfsbackup::ValidationError&) {
        }
    }
}

} // namespace

int main() {
    test_uses_fixed_controlled_command();
    test_rejects_invalid_request_before_execution();
    test_rejects_failed_or_interrupted_command();
    return test_helpers::finish("Btrfs filesystem formatter tests");
}
