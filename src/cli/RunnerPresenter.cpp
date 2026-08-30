// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/RunnerPresenter.hpp>

#include <ostream>

#include <cli/RunnerJsonCodec.hpp>

namespace btrfsbackup::cli {
namespace {

int present(const EncodedRunnerResponse& response, std::ostream& output) {
    output << response.output;
    return response.exit_code;
}

} // namespace

void print_runner_usage(std::ostream& output) {
    output << "Usage: btrfs-backupctl runner COMMAND\n"
           << "\nCommands:\n"
           << "  plan --profile ID [--offline | --mount-target] [--timestamp TS] [--run-id ID] [--mountinfo PATH]\n"
           << "  execute --profile ID [--timestamp TS] [--run-id ID] [--force] [--validate]\n"
           << "  cancel --profile ID --run-id ID\n";
}

int present_runner_plan(const btrfsbackup::backup::BackupRunPlan& plan, std::ostream& output) {
    return present(encode_runner_plan(plan), output);
}

int present_runner_execution(const btrfsbackup::backup::BackupExecutionResult& result, std::ostream& output) {
    return present(encode_runner_execution(result), output);
}

int present_runner_cancellation(const btrfsbackup::backup::CancelBackupResult& result, std::ostream& output) {
    return present(encode_runner_cancellation(result), output);
}

} // namespace btrfsbackup::cli
