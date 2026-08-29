// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/system_operational_control_backend.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <config/configuration_identity.hpp>
#include <daemon/authorized_operation_command.hpp>
#include <daemon/manager_errors.hpp>
#include <platform/linux/file_io.hpp>

namespace btrfsbackup::daemon {

namespace {

class OperationEnvironmentFile {
  public:
    OperationEnvironmentFile(
        const std::filesystem::path& root,
        const AuthorizedOperationContext& context
    ) : path_(root / (std::string(context.operation_id.value()) + ".env")) {
        btrfsbackup::platform::linux::atomic_write(
            path_,
            authorized_operation_environment(context),
            0600
        );
    }

    ~OperationEnvironmentFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    OperationEnvironmentFile(const OperationEnvironmentFile&) = delete;
    OperationEnvironmentFile& operator=(const OperationEnvironmentFile&) = delete;

  private:
    std::filesystem::path path_;
};

std::string trimmed(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

} // namespace

SystemOperationalControlBackend::SystemOperationalControlBackend(
    btrfsbackup::config::IProfileRepository& profiles,
    btrfsbackup::backup::ICancellationRequestStore& cancellation_requests,
    btrfsbackup::backup::ICommandRunner& commands,
    std::filesystem::path operation_environment_root
)
    : profiles_(profiles),
      cancellation_requests_(cancellation_requests),
      commands_(commands),
      operation_environment_root_(std::move(operation_environment_root)) {
}

OperationalResourceVersion SystemOperationalControlBackend::inspect_profile(const ProfileId& profile_id) const {
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(profile_id);
    return {
        .generation = loaded.generation,
        .fingerprint = loaded.fingerprint,
    };
}

void SystemOperationalControlBackend::require_profile_version(
    const AuthorizedOperationContext& context
) const {
    const OperationalResourceVersion expected_version{context.generation, context.fingerprint};
    if (inspect_profile(context.profile_id) != expected_version)
        throw ManagerOperationError(ManagerErrorCode::Conflict, "profile changed during authorization");
}

void SystemOperationalControlBackend::run_effect(
    const std::vector<std::string>& command,
    const char* operation
) {
    btrfsbackup::backup::ControlledCommandOptions options;
    options.timeout = std::chrono::minutes(10);
    const btrfsbackup::backup::CommandResult result = commands_.run_controlled(command, options);
    if (result.exit_code == btrfsbackup::config::configuration_changed_exit_code)
        throw ManagerOperationError(ManagerErrorCode::Conflict, "profile changed before operation execution");
    if (result.exit_code != 0 || result.timed_out || result.cancelled)
        throw ManagerOperationError(ManagerErrorCode::TargetUnavailable, std::string(operation) + " failed");
}

void SystemOperationalControlBackend::start_backup(const AuthorizedOperationContext& context) {
    require_profile_version(context);
    run_effect(authorized_backup_command(context), "starting backup");
}

ManagerCancellationOutcome SystemOperationalControlBackend::cancel_backup(
    const RunId& run_id,
    const AuthorizedOperationContext& context
) {
    require_profile_version(context);
    const auto outcome = cancellation_requests_.request_cancel({context.profile_id, run_id});
    switch (outcome) {
    case btrfsbackup::backup::CancellationRequestOutcome::Accepted:
        return ManagerCancellationOutcome::Accepted;
    case btrfsbackup::backup::CancellationRequestOutcome::StaleRun:
        return ManagerCancellationOutcome::StaleRun;
    case btrfsbackup::backup::CancellationRequestOutcome::RunMismatch:
        return ManagerCancellationOutcome::RunMismatch;
    }
    throw ManagerOperationError(ManagerErrorCode::InternalError, "unknown cancellation outcome");
}

void SystemOperationalControlBackend::validate_target(const AuthorizedOperationContext& context) {
    require_profile_version(context);
    run_target_validation(context);
}

void SystemOperationalControlBackend::run_target_validation(
    const AuthorizedOperationContext& context
) {
    OperationEnvironmentFile environment(operation_environment_root_, context);
    btrfsbackup::backup::ControlledCommandOptions options;
    options.timeout = std::chrono::minutes(11);
    const btrfsbackup::backup::CommandResult result = commands_.run_controlled(
        authorized_target_validation_command(context),
        options
    );
    if (result.exit_code == 0 && !result.timed_out && !result.cancelled) {
        return;
    }

    if (result.timed_out || result.cancelled) {
        (void)commands_.run({"systemctl", "stop", authorized_target_validation_unit(context)});
        (void)commands_.run({"systemctl", "reset-failed", authorized_target_validation_unit(context)});
        throw ManagerOperationError(ManagerErrorCode::TargetUnavailable, "validating target did not complete");
    }

    const btrfsbackup::backup::CommandResult status = commands_.run(
        authorized_target_validation_status_command(context)
    );
    const bool configuration_changed = status.exit_code == 0 &&
        trimmed(status.output) == std::to_string(btrfsbackup::config::configuration_changed_exit_code);
    (void)commands_.run({"systemctl", "reset-failed", authorized_target_validation_unit(context)});
    if (configuration_changed) {
        throw ManagerOperationError(ManagerErrorCode::Conflict, "profile changed before operation execution");
    }
    throw ManagerOperationError(ManagerErrorCode::TargetUnavailable, "validating target failed");
}

void SystemOperationalControlBackend::eject_target(const AuthorizedOperationContext& context) {
    require_profile_version(context);
    run_effect(authorized_target_eject_command(context), "ejecting target");
}

} // namespace btrfsbackup::daemon
