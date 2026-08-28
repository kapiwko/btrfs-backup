// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/hook_action_handler.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <backup/ports/command_runner.hpp>
#include <core/errors.hpp>

namespace btrfsbackup::backup {

namespace {

ErrorCode hook_error_code(const RunHookAction& action, bool timed_out) {
    if (action.phase == HookPhase::BeforeSnapshot) {
        return timed_out ? ErrorCode::HookBeforeSnapshotTimeout : ErrorCode::HookBeforeSnapshotFailed;
    }
    return timed_out ? ErrorCode::HookAfterSnapshotTimeout : ErrorCode::HookAfterSnapshotFailed;
}

} // namespace

HookActionHandler::HookActionHandler(
    ICommandRunner& commands,
    const ITrustedExecutableResolver& executables
)
    : commands_(commands),
      executables_(executables) {
}

void HookActionHandler::handle(
    const RunHookAction& action,
    const ProfileId& profile_id,
    CancellationToken& cancellation
) {
    if (action.hook.program.empty()) {
        throw ValidationError("hook program is required");
    }
    if (action.hook.timeout < std::chrono::seconds{1} || action.hook.timeout > std::chrono::hours{24}) {
        throw CodedValidationError(
            hook_error_code(action, false),
            "hook timeout is outside the supported range: " + action.hook.program
        );
    }

    CommandResult result;
    try {
        std::unique_ptr<ITrustedExecutable> executable = executables_.resolve(action.hook.program);
        std::vector<std::string> argv;
        argv.reserve(action.hook.arguments.size() + 1);
        argv.push_back(executable->execution_path());
        argv.insert(argv.end(), action.hook.arguments.begin(), action.hook.arguments.end());
        result = commands_.run_controlled(argv, {
                                                    .cancellation = &cancellation,
                                                    .timeout = action.hook.timeout,
                                                    .inherited_fds = executable->inherited_fds(),
                                                    .environment = {
                                                        {"BTRFS_BACKUP_PROFILE_ID", std::string(profile_id.value())},
                                                        {"BTRFS_BACKUP_SOURCE_ID", std::string(action.source_id.value())},
                                                    },
                                                });
    } catch (const std::exception& error) {
        throw CodedOperationError(
            hook_error_code(action, false),
            "hook execution failed: " + action.hook.program + ": " + error.what()
        );
    }
    if (result.cancelled) {
        throw OperationCancelledError("hook cancelled: " + action.hook.program);
    }
    if (result.timed_out) {
        throw CodedOperationError(
            hook_error_code(action, true),
            "hook timed out after " + std::to_string(action.hook.timeout.count()) + " seconds: " + action.hook.program
        );
    }
    if (result.exit_code != 0) {
        std::string message = "hook failed with exit code " + std::to_string(result.exit_code) + ": " + action.hook.program;
        throw CodedOperationError(hook_error_code(action, false), message);
    }
}

} // namespace btrfsbackup::backup
