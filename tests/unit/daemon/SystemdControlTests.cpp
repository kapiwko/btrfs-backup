// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>

#include <backup/ports/CancellationRequestStore.hpp>
#include <daemon/control/CommandSystemdUnitController.hpp>
#include <daemon/control/DevicePreparationUnitController.hpp>
#include <daemon/control/SystemOperationalControlBackend.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>

#include "support/TestHelpers.hpp"

namespace {

namespace backup = btrfsbackup::backup;
namespace config = btrfsbackup::config;
using btrfsbackup::OperationId;
using btrfsbackup::ProfileId;
using btrfsbackup::daemon::control::AuthorizedOperationContext;
using btrfsbackup::daemon::control::ActiveUnitRequest;
using btrfsbackup::daemon::control::ActiveUnitResult;
using btrfsbackup::daemon::control::CommandSystemdUnitController;
using btrfsbackup::daemon::control::DevicePreparationDeviceAccess;
using btrfsbackup::daemon::control::SystemdDevicePreparationUnitController;
using btrfsbackup::daemon::control::ISystemdUnitController;
using btrfsbackup::daemon::dbus::ManagerErrorCode;
using btrfsbackup::daemon::dbus::ManagerOperationError;
using btrfsbackup::daemon::control::StartJobResult;
using btrfsbackup::daemon::control::StartUnitRequest;
using btrfsbackup::daemon::control::SetUnitPropertiesRequest;
using btrfsbackup::daemon::control::StopJobResult;
using btrfsbackup::daemon::control::StopUnitRequest;
using btrfsbackup::daemon::control::SystemdJobError;
using btrfsbackup::daemon::control::SystemdJobFailure;
using btrfsbackup::daemon::control::SystemOperationalControlBackend;
using btrfsbackup::daemon::control::TransientJobResult;
using btrfsbackup::daemon::control::TransientUnitRequest;

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
    StartJobResult set_unit_properties(const SetUnitPropertiesRequest&) override {
        return {};
    }
    ActiveUnitResult active_unit(const ActiveUnitRequest&) override {
        return true;
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

    test_helpers::expect_true("transient accepted", result.has_value(), "successful job was rejected");
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
        "controlled systemd environment",
        commands.controlled_options.at(0).environment_profile ==
                backup::CommandEnvironmentProfile::SystemdControl &&
            commands.controlled_options.at(0).environment.empty(),
        "systemd command did not select its controlled environment profile"
    );
}

void test_runtime_property_failure_prevents_unit_start() {
    FakeCommands commands;
    commands.results.push_back({.exit_code = 1, .output = "Access denied"});
    CommandSystemdUnitController units(commands);
    const auto result = units.start_unit({
        .unit = "restricted.service",
        .timeout = std::chrono::seconds(5),
        .runtime_properties = {"DevicePolicy=closed"},
    });
    test_helpers::expect_true(
        "runtime property failure",
        !result && result.error().failure == SystemdJobFailure::ManagerRejected &&
            commands.calls == std::vector<std::vector<std::string>>{{
                                  "systemctl",
                                  "set-property",
                                  "--runtime",
                                  "restricted.service",
                                  "DevicePolicy=closed",
                              }},
        "unit started without its required runtime restrictions"
    );
}

void test_device_preparation_unit_receives_secret_over_fifo() {
    const auto root = test_helpers::test_root("systemd-control", "device-preparation-secret");
    FakeCommands commands;
    CommandSystemdUnitController systemd_units(commands);
    SystemdDevicePreparationUnitController units(systemd_units, root);
    constexpr std::string_view expected = "helper secret";
    const auto bytes = std::as_bytes(std::span(expected.data(), expected.size()));
    auto secret = btrfsbackup::platform::linux::filesystem::create_sealed_secret_file(bytes);
    std::string received;
    std::jthread reader([&] {
        const auto fifo = units.secret_path("prepare-fifo-test");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!std::filesystem::exists(fifo) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const int descriptor = ::open(fifo.c_str(), O_RDONLY | O_CLOEXEC);
        if (descriptor < 0)
            return;
        std::array<char, 64> buffer{};
        const ssize_t count = ::read(descriptor, buffer.data(), buffer.size());
        if (count > 0)
            received.assign(buffer.data(), static_cast<std::size_t>(count));
        ::close(descriptor);
    });

    units.start(
        "prepare-fifo-test",
        secret.get(),
        DevicePreparationDeviceAccess{{"8:17", "8:16", "8:17"}, true}
    );
    reader.join();
    test_helpers::expect_eq("device preparation FIFO secret", received, std::string(expected));
    test_helpers::expect_true(
        "device preparation start command",
        commands.calls == std::vector<std::vector<std::string>>{
                              {
                                  "systemctl",
                                  "set-property",
                                  "--runtime",
                                  "btrfs-backup-device-preparation@prepare-fifo-test.service",
                                  "DevicePolicy=closed",
                                  "DeviceAllow=",
                                  "DeviceAllow=/dev/block/8:16 rw",
                                  "DeviceAllow=/dev/block/8:17 rw",
                                  "DeviceAllow=/dev/mapper/control rw",
                              },
                              {
                                  "systemctl",
                                  "start",
                                  "--no-block",
                                  "btrfs-backup-device-preparation@prepare-fifo-test.service",
                              },
                          },
        "helper unit start command changed"
    );
    test_helpers::expect_true(
        "device preparation FIFO removed",
        !std::filesystem::exists(units.secret_path("prepare-fifo-test")),
        "secret FIFO remained on disk"
    );
    units.update_access(
        "prepare-fifo-test",
        DevicePreparationDeviceAccess{{"259:3", "8:16"}, false}
    );
    test_helpers::expect_true(
        "device preparation access replacement",
        commands.calls.back() == std::vector<std::string>{
                                     "systemctl",
                                     "set-property",
                                     "--runtime",
                                     "btrfs-backup-device-preparation@prepare-fifo-test.service",
                                     "DevicePolicy=closed",
                                     "DeviceAllow=",
                                     "DeviceAllow=/dev/block/259:3 rw",
                                     "DeviceAllow=/dev/block/8:16 rw",
                                 },
        "helper device access was not replaced with exact device numbers"
    );
}

void test_command_adapter_classifies_systemd_failures() {
    FakeCommands commands;
    commands.results.push_back({.exit_code = 5, .output = "Unit missing.service could not be found.\n"});
    CommandSystemdUnitController units(commands);
    const auto missing = units.stop_unit({"missing.service", std::chrono::seconds(5)});
    test_helpers::expect_true(
        "unit not found",
        !missing && missing.error().failure == SystemdJobFailure::UnitNotFound,
        "missing unit lost its systemd meaning"
    );

    commands.results.push_back({.exit_code = 1, .output = "Transaction is destructive."});
    const auto conflict = units.stop_unit({"busy.service", std::chrono::seconds(5)});
    test_helpers::expect_true(
        "job conflict",
        !conflict && conflict.error().failure == SystemdJobFailure::JobConflict,
        "conflicting job lost its systemd meaning"
    );

    commands.results.push_back({.exit_code = 1, .output = "Access denied by org.freedesktop.systemd1.Manager"});
    const auto rejected = units.stop_unit({"denied.service", std::chrono::seconds(5)});
    test_helpers::expect_true(
        "manager rejected",
        !rejected && rejected.error().failure == SystemdJobFailure::ManagerRejected,
        "manager rejection lost its systemd meaning"
    );
}

void test_command_adapter_reports_unit_activity() {
    FakeCommands commands;
    commands.results.push_back({.exit_code = 0, .output = "active\n"});
    commands.results.push_back({.exit_code = 3, .output = "activating\n"});
    commands.results.push_back({.exit_code = 3, .output = "inactive\n"});
    CommandSystemdUnitController units(commands);

    const auto active = units.active_unit({"active.service", std::chrono::seconds(5)});
    const auto activating = units.active_unit({"activating.service", std::chrono::seconds(5)});
    const auto inactive = units.active_unit({"inactive.service", std::chrono::seconds(5)});
    test_helpers::expect_true(
        "active unit",
        active && *active,
        "active systemd unit was not reported"
    );
    test_helpers::expect_true(
        "activating unit",
        activating && *activating,
        "activating systemd unit was not reported"
    );
    test_helpers::expect_true(
        "inactive unit",
        inactive && !*inactive,
        "inactive systemd unit was not reported"
    );
    test_helpers::expect_true(
        "activity commands",
        commands.calls == std::vector<std::vector<std::string>>{
                              {"systemctl", "is-active", "active.service"},
                              {"systemctl", "is-active", "activating.service"},
                              {"systemctl", "is-active", "inactive.service"},
                          },
        "unit activity command changed"
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
        !result &&
            result.error().unit_exit_status == config::configuration_changed_exit_code,
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
        units.transient_result = std::unexpected(SystemdJobError{failure, "detail", std::nullopt});
        expect_manager_error(
            "typed backend mapping",
            expected,
            [&] { backend.start_backup(context(profiles)); }
        );
    }

    units.transient_result = std::unexpected(SystemdJobError{
        SystemdJobFailure::UnitFailed,
        "runner failed",
        config::configuration_changed_exit_code,
    });
    expect_manager_error(
        "configuration exit status",
        ManagerErrorCode::Conflict,
        [&] { backend.start_backup(context(profiles)); }
    );
}

void test_backend_preserves_private_systemd_diagnostic() {
    FakeProfiles profiles;
    FakeCancellationRequests cancellations;
    FakeUnits units;
    units.transient_result = std::unexpected(SystemdJobError{
        SystemdJobFailure::UnitFailed,
        "Unit name collides with an installed template",
        1,
    });
    SystemOperationalControlBackend backend(profiles, cancellations, units, "/tmp/btrfs-backup-systemd-control-tests");

    try {
        backend.eject_target(context(profiles));
        test_helpers::fail("systemd failure diagnostic", "failed eject was accepted");
    } catch (const ManagerOperationError& error) {
        test_helpers::expect_contains(
            "systemd failure diagnostic",
            error.what(),
            "Unit name collides with an installed template"
        );
    }
}

} // namespace

int main() {
    test_command_adapter_builds_transient_invocation();
    test_runtime_property_failure_prevents_unit_start();
    test_device_preparation_unit_receives_secret_over_fifo();
    test_command_adapter_classifies_systemd_failures();
    test_command_adapter_reports_unit_activity();
    test_waited_transient_job_preserves_service_exit_status();
    test_backend_maps_typed_systemd_failures();
    test_backend_preserves_private_systemd_diagnostic();
    return test_helpers::finish("systemd control tests");
}
