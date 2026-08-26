// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <platform/linux/posix_command_runner.hpp>

#include "support/validation_test_helpers.hpp"

namespace {

class FakeCommandRunner final : public btrfsbackup::ICommandRunner {
public:
    btrfsbackup::CommandResult next_result;
    std::vector<std::vector<std::string>> calls;

    btrfsbackup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        return next_result;
    }

    btrfsbackup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const btrfsbackup::ControlledCommandOptions&
    ) override {
        return run(argv);
    }
};

void test_capture_command_uses_argv_without_shell() {
    FakeCommandRunner runner;
    runner.next_result = {.exit_code = 0, .output = "value\n"};
    std::string output = btrfsbackup::capture_command(runner, {"printf", "%s", "a; rm -rf /"});

    test_helpers::expect_eq("capture output trims newline", output, "value");
    test_helpers::expect_eq("capture call count", std::to_string(runner.calls.size()), "1");
    test_helpers::expect_eq("capture argv command", runner.calls.at(0).at(0), "printf");
    test_helpers::expect_eq("capture argv literal", runner.calls.at(0).at(2), "a; rm -rf /");
}

void test_capture_command_failure() {
    FakeCommandRunner runner;
    runner.next_result = {.exit_code = 2, .output = "bad\n"};
    test_helpers::expect_validation_error("capture failure", [&] {
        (void)btrfsbackup::capture_command(runner, {"false"});
    }, "command failed: false");
}

} // namespace

int main() {
    test_capture_command_uses_argv_without_shell();
    test_capture_command_failure();
    return test_helpers::finish("command runner tests");
}
