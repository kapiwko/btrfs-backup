// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <platform/linux/process.hpp>
#include <platform/linux/process_spawn.hpp>
#include <core/cancellation.hpp>

#include "support/test_helpers.hpp"

namespace {

class EnvironmentGuard {
public:
    explicit EnvironmentGuard(std::string name) : name_(std::move(name)) {
        if (const char* value = std::getenv(name_.c_str())) {
            value_ = value;
        }
    }

    ~EnvironmentGuard() {
        if (value_.has_value()) {
            setenv(name_.c_str(), value_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    std::optional<std::string> value_;
};

std::set<std::string> environment_lines(const std::string& output) {
    std::set<std::string> environment;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        environment.insert(std::move(line));
    }
    return environment;
}

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

void test_run_command_ignores_untrusted_path() {
    const char* original_path = std::getenv("PATH");
    const std::string saved_path = original_path == nullptr ? "" : original_path;
    setenv("PATH", "/definitely-untrusted", 1);
    btrfsbackup::CommandResult result = btrfsbackup::run_command({
        "sh",
        "-c",
        "printf '%s' \"$PATH\"",
    });
    if (original_path == nullptr) {
        unsetenv("PATH");
    } else {
        setenv("PATH", saved_path.c_str(), 1);
    }

    test_helpers::expect_eq("trusted command exit", std::to_string(result.exit_code), "0");
    test_helpers::expect_eq("trusted child path", result.output, "/usr/bin");
}

void test_run_command_uses_environment_allowlist() {
    EnvironmentGuard python_path("PYTHONPATH");
    EnvironmentGuard bash_env("BASH_ENV");
    EnvironmentGuard xdg_runtime_dir("XDG_RUNTIME_DIR");
    EnvironmentGuard home("HOME");
    setenv("PYTHONPATH", "/tmp/untrusted-python", 1);
    setenv("BASH_ENV", "/tmp/untrusted-bash-env", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp/untrusted-runtime", 1);
    setenv("HOME", "/tmp/untrusted-home", 1);

    btrfsbackup::CommandResult result = btrfsbackup::run_command({"env"});

    std::set<std::string> environment = environment_lines(result.output);
    const std::set<std::string> expected{
        "HOME=/root",
        "LANG=C.UTF-8",
        "LC_ALL=C.UTF-8",
        "PATH=/usr/bin",
    };

    test_helpers::expect_eq("allowlisted environment exit", std::to_string(result.exit_code), "0");
    test_helpers::expect_true(
        "allowlisted environment",
        environment == expected,
        "child inherited variables outside the environment allowlist"
    );
}

void test_run_command_limits_captured_output() {
    btrfsbackup::CommandResult result = btrfsbackup::run_command({"seq", "1", "300000"});

    test_helpers::expect_eq("default bounded command exit", std::to_string(result.exit_code), "0");
    test_helpers::expect_eq(
        "default bounded command output",
        std::to_string(result.output.size()),
        std::to_string(btrfsbackup::default_command_max_output_bytes)
    );
}

void test_controlled_command_adds_explicit_backup_context() {
    btrfsbackup::CommandResult result = btrfsbackup::run_controlled_command(
        {"env"},
        {
            .timeout = std::chrono::seconds(1),
            .profile_id = btrfsbackup::ProfileId{"laptop"},
            .source_id = btrfsbackup::SourceId{"home"},
        }
    );
    const std::set<std::string> expected{
        "BTRFS_BACKUP_PROFILE_ID=laptop",
        "BTRFS_BACKUP_SOURCE_ID=home",
        "HOME=/root",
        "LANG=C.UTF-8",
        "LC_ALL=C.UTF-8",
        "PATH=/usr/bin",
    };

    test_helpers::expect_eq("context environment exit", std::to_string(result.exit_code), "0");
    test_helpers::expect_true(
        "context environment",
        environment_lines(result.output) == expected,
        "controlled child received an unexpected environment"
    );
}

void test_run_command_rejects_relative_program_path() {
    test_helpers::expect_validation_error("relative program path", [] {
        (void)btrfsbackup::run_command({"./command"});
    }, "command path must be absolute");
}

void test_run_command_rejects_empty_program() {
    test_helpers::expect_validation_error("empty program", [] {
        (void)btrfsbackup::run_command({""});
    }, "command program must not be empty");
}

void test_controlled_command_times_out_and_reaps_process() {
    const auto started_at = std::chrono::steady_clock::now();
    btrfsbackup::CommandResult result = btrfsbackup::run_controlled_command(
        {"/bin/sh", "-c", "trap '' TERM; while :; do sleep 1; done"},
        {
            .timeout = std::chrono::milliseconds(100),
            .terminate_grace_period = std::chrono::milliseconds(50),
            .kill_reap_period = std::chrono::milliseconds(500),
        }
    );
    const auto elapsed = std::chrono::steady_clock::now() - started_at;

    test_helpers::expect_true("controlled command timed out", result.timed_out, "missing timeout result");
    test_helpers::expect_true("controlled timeout bounded", elapsed < std::chrono::seconds(2), "timeout cleanup took too long");
}

void test_controlled_command_observes_cancellation() {
    btrfsbackup::CancellationToken cancellation;
    std::thread cancel([&cancellation] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        cancellation.request_cancel();
    });

    btrfsbackup::CommandResult result = btrfsbackup::run_controlled_command(
        {"/bin/sleep", "30"},
        {
            .cancellation = &cancellation,
            .timeout = std::chrono::seconds(5),
        }
    );
    cancel.join();

    test_helpers::expect_true("controlled command cancelled", result.cancelled, "missing cancellation result");
    test_helpers::expect_true("cancel is not timeout", !result.timed_out, "cancellation was reported as timeout");
}

void test_controlled_command_bounds_captured_output() {
    btrfsbackup::CommandResult result = btrfsbackup::run_controlled_command(
        {"/usr/bin/seq", "1", "100000"},
        {
            .timeout = std::chrono::seconds(5),
            .max_output_bytes = 1024,
        }
    );

    test_helpers::expect_eq("bounded output exit", std::to_string(result.exit_code), "0");
    test_helpers::expect_eq("bounded output size", std::to_string(result.output.size()), "1024");
}

void test_noisy_controlled_command_still_observes_timeout() {
    const auto started_at = std::chrono::steady_clock::now();
    btrfsbackup::CommandResult result = btrfsbackup::run_controlled_command(
        {"/usr/bin/yes"},
        {
            .timeout = std::chrono::milliseconds(50),
            .max_output_bytes = 1024,
            .terminate_grace_period = std::chrono::milliseconds(50),
            .kill_reap_period = std::chrono::milliseconds(500),
        }
    );
    const auto elapsed = std::chrono::steady_clock::now() - started_at;

    test_helpers::expect_true("noisy command timed out", result.timed_out, "continuous output hid the timeout");
    test_helpers::expect_eq("noisy output bounded", std::to_string(result.output.size()), "1024");
    test_helpers::expect_true("noisy timeout bounded", elapsed < std::chrono::seconds(2), "noisy command cleanup took too long");
}

void test_child_process_reaps_group_during_exception_unwind() {
    int ready_pipe[2];
    test_helpers::expect_eq(
        "create child readiness pipe",
        std::to_string(pipe2(ready_pipe, O_CLOEXEC)),
        "0"
    );
    btrfsbackup::ProcessSpawnResult spawned = btrfsbackup::spawn_program(
        {"sh", "-c", "trap '' TERM; printf r; while :; do sleep 1; done"},
        {
            .stdout_fd = ready_pipe[1],
            .create_process_group = true,
        }
    );
    close(ready_pipe[1]);
    test_helpers::expect_true("spawn RAII child", spawned.started(), "child did not start");
    const pid_t child_pid = spawned.pid;
    auto started_at = std::chrono::steady_clock::now();

    try {
        btrfsbackup::ChildProcess child(
            child_pid,
            true,
            {
                .terminate_grace_period = std::chrono::milliseconds(50),
                .kill_reap_period = std::chrono::milliseconds(500),
            }
        );
        char ready = 0;
        test_helpers::expect_eq("RAII child ready", std::to_string(read(ready_pipe[0], &ready, 1)), "1");
        throw std::runtime_error("injected failure after spawn");
    } catch (const std::runtime_error&) {
    }
    close(ready_pipe[0]);

    int status = 0;
    errno = 0;
    pid_t waited = waitpid(child_pid, &status, WNOHANG);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at
    ).count();
    test_helpers::expect_eq("RAII child already reaped", std::to_string(waited), "-1");
    test_helpers::expect_eq("RAII child wait status", std::to_string(errno), std::to_string(ECHILD));
    test_helpers::expect_true("RAII cleanup bounded", elapsed_ms < 1500, "child cleanup exceeded its deadline");
}

} // namespace

int main() {
    test_run_command_captures_stdout_and_stderr();
    test_run_command_reports_missing_executable();
    test_run_command_ignores_untrusted_path();
    test_run_command_uses_environment_allowlist();
    test_run_command_limits_captured_output();
    test_controlled_command_adds_explicit_backup_context();
    test_run_command_rejects_relative_program_path();
    test_run_command_rejects_empty_program();
    test_controlled_command_times_out_and_reaps_process();
    test_controlled_command_observes_cancellation();
    test_controlled_command_bounds_captured_output();
    test_noisy_controlled_command_still_observes_timeout();
    test_child_process_reaps_group_during_exception_unwind();

    return test_helpers::finish("process tests");
}
