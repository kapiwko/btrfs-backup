// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/execution/actions/HookActionHandler.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <backup/ports/ICommandRunner.hpp>
#include <core/Errors.hpp>

namespace btrfsbackup::backup::execution {

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
    if (action.hook.program.value().empty()) {
        throw ValidationError("hook program is required");
    }
    if (action.hook.timeout < std::chrono::seconds{1} || action.hook.timeout > std::chrono::hours{24}) {
        throw CodedValidationError(
            hook_error_code(action, false),
            "hook timeout is outside the supported range: " + action.hook.program.value().string()
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
            "hook execution failed: " + action.hook.program.value().string() + ": " + error.what()
        );
    }
    if (result.cancelled) {
        throw OperationCancelledError("hook cancelled: " + action.hook.program.value().string());
    }
    if (result.timed_out) {
        throw CodedOperationError(
            hook_error_code(action, true),
            "hook timed out after " + std::to_string(action.hook.timeout.count()) + " seconds: " + action.hook.program.value().string()
        );
    }
    if (result.exit_code != 0) {
        std::string message = "hook failed with exit code " + std::to_string(result.exit_code) + ": " + action.hook.program.value().string();
        throw CodedOperationError(hook_error_code(action, false), message);
    }
}

} // namespace btrfsbackup::backup::execution
