#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <btrfsbackup/backup_tool.hpp>
#include <btrfsbackup/process.hpp>
#include <btrfsbackup/transfer_pipeline.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

std::string join(const std::vector<std::string>& args) {
    std::string out;
    for (const std::string& arg : args) {
        if (!out.empty()) {
            out += ' ';
        }
        out += arg;
    }
    return out;
}

btrfsbackup::Profile profile_with_auto_eject(bool auto_eject) {
    btrfsbackup::Profile profile;
    profile.id = "default";
    profile.name = "Default backup";
    profile.settings.auto_eject = auto_eject;
    return profile;
}

class BackupToolFixture {
public:
    int runner_status = 0;
    bool auto_eject = true;
    bool service_invocation = false;
    std::vector<std::string> runner_calls;
    std::vector<std::string> target_calls;
    std::vector<std::string> loaded_profiles;

    btrfsbackup::BackupToolServices services() {
        return {
            [this](const std::vector<std::string>& args, std::ostream& output) {
                runner_calls.push_back(join(args));
                output << "runner\n";
                return runner_status;
            },
            [this](const std::vector<std::string>& args, std::ostream& output) {
                target_calls.push_back(join(args));
                output << "target\n";
                return 0;
            },
            [this](const std::string& profile_id) {
                loaded_profiles.push_back(profile_id);
                return profile_with_auto_eject(auto_eject);
            },
            [this] {
                return service_invocation;
            }
        };
    }
};

void test_help_does_not_run_backup() {
    BackupToolFixture fixture;
    btrfsbackup::BackupToolServices services = fixture.services();
    std::ostringstream output;

    int result = btrfsbackup::backup_tool("/etc/btrfs-backup", {"--help"}, output, &services);

    test_helpers::expect_eq("backup help result", std::to_string(result), "0");
    test_helpers::expect_contains("backup help output", output.str(), "Usage: btrfs-backup");
    test_helpers::expect_true("backup help runner", fixture.runner_calls.empty(), "runner should not run");
}

void test_manual_run_executes_runner_and_ejects() {
    BackupToolFixture fixture;
    btrfsbackup::BackupToolServices services = fixture.services();
    std::ostringstream output;

    int result = btrfsbackup::backup_tool(
        "/etc/btrfs-backup",
        {"--profile", "laptop", "--force", "--validate"},
        output,
        &services
    );

    test_helpers::expect_eq("backup manual result", std::to_string(result), "0");
    test_helpers::expect_eq(
        "backup manual runner",
        fixture.runner_calls.empty() ? "" : fixture.runner_calls.front(),
        "execute --profile laptop --force --validate"
    );
    test_helpers::expect_eq(
        "backup manual eject",
        fixture.target_calls.empty() ? "" : fixture.target_calls.front(),
        "eject --from-runner --profile laptop"
    );
}

void test_no_eject_skips_target_command() {
    BackupToolFixture fixture;
    btrfsbackup::BackupToolServices services = fixture.services();
    std::ostringstream output;

    int result = btrfsbackup::backup_tool("/etc/btrfs-backup", {"--no-eject"}, output, &services);

    test_helpers::expect_eq("backup no eject result", std::to_string(result), "0");
    test_helpers::expect_true("backup no eject target", fixture.target_calls.empty(), "target eject should not run");
}

void test_service_invocation_skips_runner_eject() {
    BackupToolFixture fixture;
    fixture.service_invocation = true;
    btrfsbackup::BackupToolServices services = fixture.services();
    std::ostringstream output;

    int result = btrfsbackup::backup_tool("/etc/btrfs-backup", {}, output, &services);

    test_helpers::expect_eq("backup service result", std::to_string(result), "0");
    test_helpers::expect_true("backup service target", fixture.target_calls.empty(), "target eject should be left to service");
}

void test_profile_auto_eject_false_skips_target_command() {
    BackupToolFixture fixture;
    fixture.auto_eject = false;
    btrfsbackup::BackupToolServices services = fixture.services();
    std::ostringstream output;

    int result = btrfsbackup::backup_tool("/etc/btrfs-backup", {}, output, &services);

    test_helpers::expect_eq("backup auto eject false result", std::to_string(result), "0");
    test_helpers::expect_true("backup auto eject false target", fixture.target_calls.empty(), "target eject should not run");
    test_helpers::expect_eq(
        "backup auto eject false loaded profile",
        fixture.loaded_profiles.empty() ? "" : fixture.loaded_profiles.front(),
        "default"
    );
}

void test_runner_failure_skips_target_command() {
    BackupToolFixture fixture;
    fixture.runner_status = 23;
    btrfsbackup::BackupToolServices services = fixture.services();
    std::ostringstream output;

    int result = btrfsbackup::backup_tool("/etc/btrfs-backup", {}, output, &services);

    test_helpers::expect_eq("backup runner failure result", std::to_string(result), "23");
    test_helpers::expect_true("backup runner failure target", fixture.target_calls.empty(), "target eject should not run");
}

void test_termination_signals_request_cancellation() {
    for (int signal : {SIGINT, SIGTERM}) {
        btrfsbackup::CancellationToken cancellation;
        btrfsbackup::TerminationSignalMonitor monitor(cancellation);
        test_helpers::expect_eq("send termination signal", std::to_string(kill(getpid(), signal)), "0");

        pollfd cancellation_fd{
            .fd = cancellation.cancellation_fd(),
            .events = POLLIN,
            .revents = 0,
        };
        test_helpers::expect_eq(
            "termination signal wakes cancellation",
            std::to_string(poll(&cancellation_fd, 1, 1000)),
            "1"
        );
        test_helpers::expect_true(
            "termination signal requests cancellation",
            cancellation.cancellation_requested(),
            "signal monitor did not request cancellation"
        );
    }
}

void test_spawned_children_do_not_inherit_blocked_termination_signals() {
    btrfsbackup::CancellationToken cancellation;
    btrfsbackup::TerminationSignalMonitor monitor(cancellation);
    btrfsbackup::CommandResult result = btrfsbackup::run_command({
        "sh",
        "-c",
        "kill -TERM $$; printf survived",
    });

    test_helpers::expect_eq("spawned child termination", std::to_string(result.exit_code), "128");
    test_helpers::expect_true("spawned child did not survive", result.output.find("survived") == std::string::npos, "child inherited blocked SIGTERM");
}

} // namespace

int main() {
    test_help_does_not_run_backup();
    test_manual_run_executes_runner_and_ejects();
    test_no_eject_skips_target_command();
    test_service_invocation_skips_runner_eject();
    test_profile_auto_eject_false_skips_target_command();
    test_runner_failure_skips_target_command();
    test_termination_signals_request_cancellation();
    test_spawned_children_do_not_inherit_blocked_termination_signals();
    return test_helpers::finish("backup tool tests passed");
}
