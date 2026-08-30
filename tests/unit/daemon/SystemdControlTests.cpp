// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <backup/ports/CancellationRequestStore.hpp>
#include <daemon/CommandSystemdUnitController.hpp>
#include <daemon/ManagerErrors.hpp>
#include <daemon/SystemOperationalControlBackend.hpp>

#include "support/TestHelpers.hpp"

namespace {

namespace backup = btrfsbackup::backup;
namespace config = btrfsbackup::config;
using btrfsbackup::OperationId;
using btrfsbackup::ProfileId;
using btrfsbackup::daemon::AuthorizedOperationContext;
using btrfsbackup::daemon::CommandSystemdUnitController;
using btrfsbackup::daemon::ISystemdUnitController;
using btrfsbackup::daemon::ManagerErrorCode;
using btrfsbackup::daemon::ManagerOperationError;
using btrfsbackup::daemon::StartJobResult;
using btrfsbackup::daemon::StartUnitRequest;
using btrfsbackup::daemon::StopJobResult;
using btrfsbackup::daemon::StopUnitRequest;
using btrfsbackup::daemon::SystemdJobError;
using btrfsbackup::daemon::SystemdJobFailure;
using btrfsbackup::daemon::SystemOperationalControlBackend;
using btrfsbackup::daemon::TransientJobResult;
using btrfsbackup::daemon::TransientUnitRequest;

class FakeCommands final : public backup::ICommandRunner {
  public:
    std::deque<backup::CommandResult> results;
    std::vector<std::vector<std::string>> calls;
    std::vector<backup::ControlledCommandOptions> controlled_options;

    backup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        return next();
    }

    backup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const backup::ControlledCommandOptions& options
    ) override {
        calls.push_back(argv);
        controlled_options.push_back(options);
        return next();
    }

  private:
    backup::CommandResult next() {
        if (results.empty())
            return {};
        auto result = results.front();
        results.pop_front();
        return result;
    }
};

class FakeProfiles final : public config::IProfileRepository {
  public:
    config::Profile profile{
        ProfileId{"default"},
        {
            config::LuksUuid{"11111111-2222-3333-4444-555555555555"},
            config::BtrfsUuid{"22222222-3333-4444-5555-666666666666"},
            config::PartitionUuid{""},
            config::MapperName{"backup"},
        },
        {
            config::RemoteSnapshotRoot{"/mnt/backup/snapshots"},
            config::IncomingRoot{"/mnt/backup/.incoming"},
        },
    };

    config::LoadedProfile get(const ProfileId&) const override {
        return {
            .profile = profile,
            .fingerprint = config::ConfigurationFingerprint{"fingerprint"},
            .generation = profile.configuration_generation,
        };
    }
};

class FakeCancellationRequests final : public backup::ICancellationRequestStore {
  public:
    std::unique_ptr<backup::IActiveRunRegistration> register_active_run(
        const backup::CancellationRequest&
    ) override {
        return {};
    }
    backup::CancellationRequestOutcome request_cancel(const backup::CancellationRequest&) override {
        return backup::CancellationRequestOutcome::Accepted;
    }
    bool cancel_requested(const backup::CancellationRequest&) const override {
        return false;
    }
    void clear_cancel_request(const backup::CancellationRequest&) override {
    }
};

class FakeUnits final : public ISystemdUnitController {
  public:
    TransientJobResult transient_result;

    StartJobResult start_unit(const StartUnitRequest&) override {
        return {};
    }
    StopJobResult stop_unit(const StopUnitRequest&) override {
        return {};
    }
    TransientJobResult start_transient_unit(const TransientUnitRequest&) override {
        return transient_result;
    }
};

AuthorizedOperationContext context(const FakeProfiles& profiles) {
    const auto loaded = profiles.get(ProfileId{"default"});
    return {
        .profile_id = ProfileId{"default"},
        .generation = loaded.generation,
        .fingerprint = loaded.fingerprint,
        .operation_id = OperationId{"operation-1"},
    };
}

void expect_manager_error(
    const std::string& name,
    ManagerErrorCode expected,
    const std::function<void()>& operation
) {
    try {
        operation();
        test_helpers::fail(name, "operation succeeded");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_true(name, error.code() == expected, "unexpected manager error");
    }
}

void test_command_adapter_builds_transient_invocation() {
    FakeCommands commands;
    CommandSystemdUnitController units(commands);
    const TransientJobResult result = units.start_transient_unit({
        .unit = "backup.service",
        .command = {"/usr/bin/true", "argument"},
        .properties = {"NoNewPrivileges=yes"},
        .environment = {"OPERATION_ID=1"},
        .timeout = std::chrono::seconds(5),
        .wait = false,
    });

    test_helpers::expect_true("transient accepted", result.accepted(), "successful job was rejected");
    test_helpers::expect_true(
        "typed request encoded",
        commands.calls == std::vector<std::vector<std::string>>{{
                              "systemd-run",
                              "--quiet",
                              "--no-block",
                              "--collect",
                              "--unit=backup.service",
                              "--property=NoNewPrivileges=yes",
                              "--setenv=OPERATION_ID=1",
                              "/usr/bin/true",
                              "argument",
                          }},
        "transient request was encoded incorrectly"
    );
    test_helpers::expect_true(
        "stable systemd locale",
        commands.controlled_options.at(0).environment.at("LC_ALL") == "C",
        "systemd error classification depends on the manager locale"
    );
}

void test_command_adapter_classifies_systemd_failures() {
    FakeCommands commands;
    commands.results.push_back({.exit_code = 5, .output = "Unit missing.service could not be found.\n"});
    CommandSystemdUnitController units(commands);
    const auto missing = units.stop_unit({"missing.service", std::chrono::seconds(5)});
    test_helpers::expect_true(
        "unit not found",
        missing.error && missing.error->failure == SystemdJobFailure::UnitNotFound,
        "missing unit lost its systemd meaning"
    );

    commands.results.push_back({.exit_code = 1, .output = "Transaction is destructive."});
    const auto conflict = units.stop_unit({"busy.service", std::chrono::seconds(5)});
    test_helpers::expect_true(
        "job conflict",
        conflict.error && conflict.error->failure == SystemdJobFailure::JobConflict,
        "conflicting job lost its systemd meaning"
    );

    commands.results.push_back({.exit_code = 1, .output = "Access denied by org.freedesktop.systemd1.Manager"});
    const auto rejected = units.stop_unit({"denied.service", std::chrono::seconds(5)});
    test_helpers::expect_true(
        "manager rejected",
        rejected.error && rejected.error->failure == SystemdJobFailure::ManagerRejected,
        "manager rejection lost its systemd meaning"
    );
}

void test_waited_transient_job_preserves_service_exit_status() {
    FakeCommands commands;
    commands.results.push_back({.exit_code = config::configuration_changed_exit_code});
    CommandSystemdUnitController units(commands);
    const auto result = units.start_transient_unit({
        .unit = "changed.service",
        .command = {"/usr/bin/false"},
        .timeout = std::chrono::seconds(5),
        .wait = true,
    });
    test_helpers::expect_true(
        "transient exit status",
        result.error &&
            result.error->unit_exit_status == config::configuration_changed_exit_code,
        "waited transient unit exit status was lost after collection"
    );
}

void test_backend_maps_typed_systemd_failures() {
    FakeProfiles profiles;
    FakeCancellationRequests cancellations;
    FakeUnits units;
    SystemOperationalControlBackend backend(profiles, cancellations, units, "/tmp/btrfs-backup-systemd-control-tests");

    const std::vector<std::pair<SystemdJobFailure, ManagerErrorCode>> mappings{
        {SystemdJobFailure::UnitNotFound, ManagerErrorCode::NotFound},
        {SystemdJobFailure::JobAlreadyRunning, ManagerErrorCode::Busy},
        {SystemdJobFailure::JobConflict, ManagerErrorCode::Conflict},
        {SystemdJobFailure::TimedOut, ManagerErrorCode::TargetUnavailable},
        {SystemdJobFailure::ManagerRejected, ManagerErrorCode::InternalError},
    };
    for (const auto& [failure, expected] : mappings) {
        units.transient_result.error = SystemdJobError{failure, "detail", std::nullopt};
        expect_manager_error(
            "typed backend mapping",
            expected,
            [&] { backend.start_backup(context(profiles)); }
        );
    }

    units.transient_result.error = SystemdJobError{
        SystemdJobFailure::UnitFailed,
        "runner failed",
        config::configuration_changed_exit_code,
    };
    expect_manager_error(
        "configuration exit status",
        ManagerErrorCode::Conflict,
        [&] { backend.start_backup(context(profiles)); }
    );
}

} // namespace

int main() {
    test_command_adapter_builds_transient_invocation();
    test_command_adapter_classifies_systemd_failures();
    test_waited_transient_job_preserves_service_exit_status();
    test_backend_maps_typed_systemd_failures();
    return test_helpers::finish("systemd control tests");
}
