// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/runner/RunnerCommand.hpp>

#include <filesystem>
#include <ostream>
#include <vector>

#include <backup/BackupService.hpp>
#include <cli/runner/RunnerComposition.hpp>
#include <cli/runner/RunnerOptions.hpp>
#include <cli/runner/RunnerPresenter.hpp>
#include <core/Cancellation.hpp>

namespace btrfsbackup::cli::runner {
namespace {

int run_with_service(
    const RunnerOptions& options,
    std::ostream& output,
    backup::BackupService& service
) {
    if (options.command == RunnerCommandKind::Cancel) {
        return present_runner_cancellation(
            service.cancel({options.request.profile_id, options.run_id}),
            output
        );
    }
    if (options.command == RunnerCommandKind::Plan) {
        return present_runner_plan(service.plan({options.request.profile_id, options.mount_target}), output);
    }
    return present_runner_execution(service.start(options.request), output);
}

bool present_help(const std::vector<std::string>& args, std::ostream& output, int& result) {
    if (args.empty()) {
        print_runner_usage(output);
        result = 2;
        return true;
    }
    if (args.front() == "-h" || args.front() == "--help") {
        print_runner_usage(output);
        result = 0;
        return true;
    }
    return false;
}

} // namespace

int runner(
    const std::vector<std::string>& args,
    std::ostream& output,
    backup::BackupService& service
) {
    int early_result = 0;
    if (present_help(args, output, early_result)) {
        return early_result;
    }
    return run_with_service(parse_runner_options(args), output, service);
}

int runner(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output
) {
    CancellationToken cancellation;
    return runner(profile_config_dir, args, output, cancellation);
}

int runner(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    CancellationToken& cancellation
) {
    int early_result = 0;
    if (present_help(args, output, early_result)) {
        return early_result;
    }
    const RunnerOptions options = parse_runner_options(args);
    RunnerComposition composition(profile_config_dir, options, cancellation);
    return run_with_service(options, output, composition.service());
}

} // namespace btrfsbackup::cli::runner
