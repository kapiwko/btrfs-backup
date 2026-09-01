// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>

#include <platform/linux/process/PosixCommandRunner.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace {

class FakeCommandRunner final : public btrfsbackup::backup::ICommandRunner {
  public:
    btrfsbackup::backup::CommandResult next_result;
    std::vector<std::vector<std::string>> calls;

    btrfsbackup::backup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        return next_result;
    }

    btrfsbackup::backup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const btrfsbackup::backup::ControlledCommandOptions&
    ) override {
        return run(argv);
    }
};

void test_capture_command_uses_argv_without_shell() {
    FakeCommandRunner runner;
    runner.next_result = {.exit_code = 0, .output = "value\n"};
    std::string output = btrfsbackup::backup::capture_command(runner, {"printf", "%s", "a; rm -rf /"});

    test_helpers::expect_eq("capture output trims newline", output, "value");
    test_helpers::expect_eq("capture call count", std::to_string(runner.calls.size()), "1");
    test_helpers::expect_eq("capture argv command", runner.calls.at(0).at(0), "printf");
    test_helpers::expect_eq("capture argv literal", runner.calls.at(0).at(2), "a; rm -rf /");
}

void test_capture_command_failure() {
    FakeCommandRunner runner;
    runner.next_result = {.exit_code = 2, .output = "bad\n"};
    test_helpers::expect_validation_error("capture failure", [&] { (void)btrfsbackup::backup::capture_command(runner, {"false"}); }, "command failed: false");
}

void test_controlled_command_reads_supplied_standard_input() {
    int descriptors[2];
    test_helpers::expect_true("stdin pipe", ::pipe(descriptors) == 0, "cannot create stdin pipe");
    constexpr std::string_view input = "partition-script\n";
    test_helpers::expect_true(
        "stdin write",
        ::write(descriptors[1], input.data(), input.size()) == static_cast<ssize_t>(input.size()),
        "cannot write stdin pipe"
    );
    ::close(descriptors[1]);
    btrfsbackup::platform::linux::process::PosixCommandRunner runner;
    btrfsbackup::backup::ControlledCommandOptions options;
    options.stdin_fd = descriptors[0];
    const auto result = runner.run_controlled({"/usr/bin/cat"}, options);
    ::close(descriptors[0]);
    test_helpers::expect_true("stdin command status", result.exit_code == 0, "cat failed");
    test_helpers::expect_eq("stdin command output", result.output, std::string(input));
}

} // namespace

int main() {
    test_capture_command_uses_argv_without_shell();
    test_capture_command_failure();
    test_controlled_command_reads_supplied_standard_input();
    return test_helpers::finish("command runner tests");
}
