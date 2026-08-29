// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/system_operational_control_backend.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

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

ManagerErrorCode manager_error_code(SystemdJobFailure failure) {
    switch (failure) {
    case SystemdJobFailure::UnitNotFound:
        return ManagerErrorCode::NotFound;
    case SystemdJobFailure::JobAlreadyRunning:
        return ManagerErrorCode::Busy;
    case SystemdJobFailure::JobConflict:
        return ManagerErrorCode::Conflict;
    case SystemdJobFailure::Cancelled:
    case SystemdJobFailure::TimedOut:
    case SystemdJobFailure::UnitFailed:
        return ManagerErrorCode::TargetUnavailable;
    case SystemdJobFailure::ManagerRejected:
        return ManagerErrorCode::InternalError;
    }
    return ManagerErrorCode::InternalError;
}

[[noreturn]] void throw_job_error(const SystemdJobError& error, const char* operation) {
    if (error.unit_exit_status == btrfsbackup::config::configuration_changed_exit_code)
        throw ManagerOperationError(ManagerErrorCode::Conflict, "profile changed before operation execution");
    throw ManagerOperationError(manager_error_code(error.failure), std::string(operation) + " failed");
}

} // namespace

SystemOperationalControlBackend::SystemOperationalControlBackend(
    btrfsbackup::config::IProfileRepository& profiles,
    btrfsbackup::backup::ICancellationRequestStore& cancellation_requests,
    ISystemdUnitController& units,
    std::filesystem::path operation_environment_root
)
    : profiles_(profiles),
      cancellation_requests_(cancellation_requests),
      units_(units),
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

void SystemOperationalControlBackend::require_job_accepted(
    const TransientJobResult& result,
    const char* operation
) {
    if (result.error)
        throw_job_error(*result.error, operation);
}

void SystemOperationalControlBackend::start_backup(const AuthorizedOperationContext& context) {
    require_profile_version(context);
    require_job_accepted(units_.start_transient_unit(authorized_backup_unit(context)), "starting backup");
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
    const std::string unit = authorized_target_validation_unit(context);
    const StartJobResult result = units_.start_unit({unit, std::chrono::minutes(11)});
    if (result.accepted()) {
        return;
    }

    if (result.error->failure == SystemdJobFailure::TimedOut ||
        result.error->failure == SystemdJobFailure::Cancelled)
        (void)units_.stop_unit({unit, std::chrono::minutes(2)});
    throw_job_error(*result.error, "validating target");
}

void SystemOperationalControlBackend::eject_target(const AuthorizedOperationContext& context) {
    require_profile_version(context);
    require_job_accepted(units_.start_transient_unit(authorized_target_eject_unit(context)), "ejecting target");
}

} // namespace btrfsbackup::daemon
