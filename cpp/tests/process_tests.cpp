#include <string>

#include <btrfsbackup/process.hpp>

#include "test_helpers.hpp"

namespace {

void test_run_command_captures_stdout_and_stderr() {
    btrfsbackup::CommandResult result = btrfsbackup::run_command({
        "sh",
        "-c",
        "printf output; printf error >&2; exit 7",
    });

    test_helpers::expect_eq("command exit", std::to_string(result.exit_code), "7");
    test_helpers::expect_contains("command stdout", result.output, "output");
    test_helpers::expect_contains("command stderr", result.output, "error");
}

void test_run_command_reports_missing_executable() {
    btrfsbackup::CommandResult result = btrfsbackup::run_command({
        "/definitely-missing-btrfsbackup-command",
    });

    test_helpers::expect_eq("missing command exit", std::to_string(result.exit_code), "127");
    test_helpers::expect_contains("missing command diagnostics", result.output, "cannot spawn");
    test_helpers::expect_contains(
        "missing command name",
        result.output,
        "/definitely-missing-btrfsbackup-command"
    );
}

} // namespace

int main() {
    test_run_command_captures_stdout_and_stderr();
    test_run_command_reports_missing_executable();

    return test_helpers::finish("process tests");
}
