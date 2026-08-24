#include <filesystem>
#include <string>
#include <vector>

#include <btrfsbackup/runtime_adapters.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

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
    runner.next_result = {
        .exit_code = 0,
        .output = "value\n",
    };

    std::string output = btrfsbackup::capture_command(
        runner,
        {"printf", "%s", "a; rm -rf /"}
    );

    test_helpers::expect_eq("capture output trims newline", output, "value");
    test_helpers::expect_eq("capture call count", std::to_string(runner.calls.size()), "1");
    test_helpers::expect_eq("capture argv command", runner.calls.at(0).at(0), "printf");
    test_helpers::expect_eq("capture argv literal", runner.calls.at(0).at(2), "a; rm -rf /");
}

void test_capture_command_failure() {
    FakeCommandRunner runner;
    runner.next_result = {
        .exit_code = 2,
        .output = "bad\n",
    };

    test_helpers::expect_validation_error("capture failure", [&] {
        (void)btrfsbackup::capture_command(runner, {"false"});
    }, "command failed: false");
}

void test_std_filesystem_effects() {
    fs::path root = test_helpers::test_root("runtime-adapters", "filesystem");
    btrfsbackup::StdFileSystemEffects fs_effects;

    fs::path directory = root / "dir";
    fs_effects.create_directories(directory);
    test_helpers::expect_true("fs directory", fs_effects.is_directory(directory), "directory should exist");

    fs::path source = directory / "source";
    fs::path target = directory / "target";
    test_helpers::write_file(source, "content");
    test_helpers::expect_true("fs file exists", fs_effects.exists(source), "source file should exist");

    fs_effects.rename_path(source, target);
    test_helpers::expect_true("fs renamed target", fs_effects.exists(target), "target file should exist");
    test_helpers::expect_true("fs renamed source", !fs_effects.exists(source), "source file should be gone");

    fs_effects.remove_file(target);
    test_helpers::expect_true("fs removed", !fs_effects.exists(target), "target file should be removed");

    fs::remove_all(root);
}

} // namespace

int main() {
    test_capture_command_uses_argv_without_shell();
    test_capture_command_failure();
    test_std_filesystem_effects();

    return test_helpers::finish("runtime adapters tests");
}
