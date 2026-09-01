// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemDeviceProvisioningBackend.hpp>
#include <daemon/control/DevicePreparationTransaction.hpp>
#include <daemon/control/DevicePreparationUnitController.hpp>

#include <algorithm>
#include <backup/ports/ICommandRunner.hpp>
#include <backup/ports/IBtrfsOperations.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <daemon/control/CredentialAdministrationService.hpp>
#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>

#include <chrono>
#include <ranges>
#include <string_view>
#include <thread>
#include <unistd.h>

#include "support/TestHelpers.hpp"

namespace {
namespace backup = btrfsbackup::backup;
namespace config = btrfsbackup::config;
using btrfsbackup::ProfileId;
using btrfsbackup::daemon::control::DevicePreparationStatus;
using btrfsbackup::daemon::control::DevicePreparationTransaction;
using btrfsbackup::daemon::control::DevicePreparationTransactionStore;
using btrfsbackup::daemon::control::ICredentialAdministrationBackend;
using btrfsbackup::daemon::control::IDestructiveDeviceSafetyInspector;
using btrfsbackup::daemon::control::IDevicePreparationUnitController;
using btrfsbackup::daemon::control::SystemDeviceProvisioningBackend;
using btrfsbackup::daemon::control::TargetCredential;

class Commands final : public backup::ICommandRunner {
  public:
    std::vector<std::vector<std::string>> calls;
    std::vector<std::vector<std::string>> controlled_calls;
    int identity_scans = 0;
    int replace_on_scan = 0;
    backup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        if (argv.front() == "lsblk") {
            if (std::ranges::find(argv, "PATH,TYPE") != argv.end())
                return {0, R"({"blockdevices":[{"path":"/dev/test","type":"disk","children":[{"path":"/dev/test1","type":"part"}]}]})"};
            ++identity_scans;
            const bool replaced = replace_on_scan != 0 && identity_scans >= replace_on_scan;
            return {0, replaced ? R"({"blockdevices":[{"path":"/dev/test","type":"disk","size":1048576,"model":"Other","serial":"OTHER","wwn":"WWN-OTHER","tran":"usb","rm":true,"fstype":null,"pttype":null,"mountpoints":[null],"maj:min":"8:16","kname":"test","pkname":null}]})" : R"({"blockdevices":[{"path":"/dev/test","type":"disk","size":1048576,"model":"Test","serial":"SERIAL","wwn":"WWN-TEST","tran":"usb","rm":true,"fstype":null,"pttype":null,"mountpoints":[null],"maj:min":"8:16","kname":"test","pkname":null}]})"};
        }
        if (argv.front() == "udevadm" && argv.size() > 1 && argv.at(1) == "info")
            return {0, replace_on_scan != 0 && identity_scans >= replace_on_scan ? "DEVPATH=/devices/other/block/test\nMAJOR=8\nMINOR=16\nID_WWN=WWN-OTHER\nID_SERIAL=OTHER\nID_SERIAL_SHORT=OTHER\nID_BUS=usb\n" : "DEVPATH=/devices/test/block/test\nMAJOR=8\nMINOR=16\nID_WWN=WWN-TEST\nID_SERIAL=VENDOR_SERIAL\nID_SERIAL_SHORT=SERIAL\nID_BUS=usb\n"};
        if (argv.front() == "cryptsetup" && argv.at(1) == "luksUUID")
            return {0, "11111111-2222-3333-4444-555555555555\n"};
        if (argv.front() == "blkid" && argv.back() == "/dev/mapper/btrfs-backup-test")
            return {0, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n"};
        if (argv.front() == "blkid")
            return {0, "99999999-8888-7777-6666-555555555555\n"};
        return {0, {}};
    }
    backup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const backup::ControlledCommandOptions&
    ) override {
        controlled_calls.push_back(argv);
        return run(argv);
    }
};

class Btrfs final : public backup::IBtrfsOperations {
  public:
    bool is_subvolume(const std::filesystem::path&) override {
        return true;
    }
    std::optional<backup::SnapshotMetadata> read_snapshot_metadata(const std::filesystem::path&) override {
        return {};
    }
    void create_readonly_snapshot(const std::filesystem::path&, const std::filesystem::path&) override {
    }
    void delete_subvolume(const std::filesystem::path&) override {
    }
};

class Credentials final : public ICredentialAdministrationBackend {
  public:
    bool registered = false;
    bool generated = false;
    std::vector<TargetCredential> list_credentials(const ProfileId&) const override {
        return {};
    }
    void add_passphrase(const ProfileId&, int, int, const std::string&) override {
    }
    void add_key(const ProfileId&, int, int, const std::string&, bool) override {
    }
    void generate_key(const ProfileId&, int, const std::string&, bool automatic) override {
        generated = automatic;
    }
    void remove_credential(const ProfileId&, const std::string&, int) override {
    }
    void register_initial_passphrase(const ProfileId&, int slot, const std::string&) override {
        registered = slot == 0;
    }
};

class SafetyInspector final : public IDestructiveDeviceSafetyInspector {
  public:
    mutable int inspections = 0;
    std::vector<std::string> reasons;
    std::vector<std::string> inspect(const btrfsbackup::daemon::control::ProvisioningDevice&) const override {
        ++inspections;
        return reasons;
    }
};

class Units final : public IDevicePreparationUnitController {
  public:
    bool running = false;
    int starts = 0;
    int recoveries = 0;
    int stops = 0;
    void start(const std::string&, int) override {
        running = true;
        ++starts;
    }
    void recover(const std::string&) override {
        ++recoveries;
    }
    void stop(const std::string&) override {
        running = false;
        ++stops;
    }
    bool active(const std::string&) override {
        return running;
    }
};

int secret_descriptor(std::string_view secret) {
    int descriptors[2];
    test_helpers::expect_true("helper secret pipe", ::pipe(descriptors) == 0, "cannot create helper pipe");
    static_cast<void>(::write(descriptors[1], secret.data(), secret.size()));
    ::close(descriptors[1]);
    return descriptors[0];
}

void test_preparation_sequence_uses_descriptors_and_installs_profile() {
    const auto root = test_helpers::test_root("device-provisioning", "success");
    Commands commands;
    Btrfs btrfs;
    Credentials credentials;
    SafetyInspector safety;
    Units units;
    config::NullConfigurationActivator activator;
    SystemDeviceProvisioningBackend backend(
        {
            .config_root = root / "etc",
            .metadata_root = root / "etc/credentials",
            .key_root = root / "etc/keys",
            .lock_root = root / "run/locks",
            .udev_root = root / "udev",
            .systemd_root = root / "systemd",
            .public_root = root / "public",
        },
        root / "mnt",
        root / "mountinfo",
        root / "transactions",
        commands,
        btrfs,
        activator,
        credentials,
        safety,
        units
    );
    int descriptors[2];
    test_helpers::expect_true("secret pipe", ::pipe(descriptors) == 0, "cannot create pipe");
    constexpr std::string_view password = "secret";
    static_cast<void>(::write(descriptors[1], password.data(), password.size()));
    ::close(descriptors[1]);
    const auto candidates = backend.list_devices();
    test_helpers::expect_true("identity candidate", candidates.size() == 1, "device identity was incomplete");
    const auto started = backend.start(
        {
            .profile_id = "test",
            .profile_name = "Test backup",
            .candidate_id = "candidate-test",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
            .create_automatic_key = true,
        },
        candidates.front(),
        {.bus_name = ":1.5", .uid = 1000},
        descriptors[0]
    );
    ::close(descriptors[0]);
    test_helpers::expect_true("helper launched", units.starts == 1, "manager did not launch the helper unit");
    test_helpers::expect_true(
        "manager does not wipe",
        std::ranges::find(commands.calls, std::string("wipefs"), [](const auto& call) {
            return call.front();
        }) == commands.calls.end(),
        "destructive command ran on the manager start path"
    );
    const int helper_secret = secret_descriptor(password);
    backend.execute_operation(started.operation_id, helper_secret);
    ::close(helper_secret);
    const DevicePreparationStatus status = backend.status(started.operation_id);
    test_helpers::expect_eq("preparation state", status.state, "succeeded");
    test_helpers::expect_true("initial credential", credentials.registered, "initial passphrase not registered");
    test_helpers::expect_true("automatic credential", credentials.generated, "automatic key not generated");
    test_helpers::expect_true(
        "profile installed",
        std::filesystem::exists(root / "etc/profiles/test/profile.json"),
        "profile was not installed"
    );
    const auto wipe = std::ranges::find(commands.calls, std::string("wipefs"), [](const auto& call) { return call.front(); });
    const auto partition = std::ranges::find(commands.calls, std::string("sfdisk"), [](const auto& call) { return call.front(); });
    const auto format = std::ranges::find(commands.calls, std::string("mkfs.btrfs"), [](const auto& call) { return call.front(); });
    test_helpers::expect_true(
        "destructive command order",
        wipe != commands.calls.end() && partition != commands.calls.end() && format != commands.calls.end() &&
            wipe < partition && partition < format,
        "device commands ran out of order"
    );
    test_helpers::expect_true(
        "identity checked immediately before wipe",
        commands.identity_scans >= 3,
        "device identity was not checked twice after candidate issuance"
    );
    test_helpers::expect_true("safety inspection", safety.inspections == 1, "device safety was not rechecked");
    test_helpers::expect_true(
        "udev settle checked",
        std::ranges::count_if(commands.controlled_calls, [](const auto& call) {
            return call == std::vector<std::string>{"udevadm", "settle", "--timeout=10"};
        }) == 2,
        "udev settle was not checked after partition and filesystem creation"
    );
    const auto partition_scan = std::ranges::find_if(commands.calls, [](const auto& call) {
        return !call.empty() && call.front() == "lsblk" &&
            std::ranges::find(call, "PATH,TYPE") != call.end();
    });
    test_helpers::expect_true(
        "partition scan requests tree",
        partition_scan != commands.calls.end() &&
            std::ranges::find(*partition_scan, "--tree") != partition_scan->end(),
        "partition discovery expected lsblk children without requesting a tree"
    );
}

void test_exited_helper_marks_transaction_interrupted() {
    const auto root = test_helpers::test_root("device-provisioning", "helper-exited");
    Commands commands;
    Btrfs btrfs;
    Credentials credentials;
    SafetyInspector safety;
    Units units;
    config::NullConfigurationActivator activator;
    SystemDeviceProvisioningBackend backend(
        {
            .config_root = root / "etc",
            .metadata_root = root / "etc/credentials",
            .key_root = root / "etc/keys",
            .lock_root = root / "run/locks",
            .udev_root = root / "udev",
            .systemd_root = root / "systemd",
            .public_root = root / "public",
        },
        root / "mnt",
        root / "mountinfo",
        root / "transactions",
        commands,
        btrfs,
        activator,
        credentials,
        safety,
        units
    );
    const auto candidate = backend.list_devices().front();
    const int secret = secret_descriptor("secret");
    const auto started = backend.start(
        {
            .profile_id = "helper-exited",
            .profile_name = "Helper exited",
            .candidate_id = "candidate-helper-exited",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
        },
        candidate,
        {.bus_name = ":1.7", .uid = 1000},
        secret
    );
    ::close(secret);
    auto persisted = DevicePreparationTransactionStore(root / "transactions").load(started.operation_id);
    persisted.mapper = "btrfs-backup-helper-exited";
    DevicePreparationTransactionStore(root / "transactions").save(persisted);
    units.running = false;

    const auto status = backend.status(started.operation_id);
    test_helpers::expect_eq("exited helper state", status.state, "interrupted");
    test_helpers::expect_eq(
        "exited helper error",
        status.error_code,
        "device-preparation.helper-exited"
    );
    test_helpers::expect_true(
        "exited helper cannot cancel",
        !status.can_cancel,
        "interrupted helper remained cancellable"
    );
    test_helpers::expect_true(
        "exited helper cleanup launched",
        units.recoveries == 1,
        "recorded mapper cleanup helper was not launched"
    );
}

void test_replacement_before_wipe_is_rejected() {
    const auto root = test_helpers::test_root("device-provisioning", "replacement");
    Commands commands;
    Btrfs btrfs;
    Credentials credentials;
    SafetyInspector safety;
    Units units;
    config::NullConfigurationActivator activator;
    SystemDeviceProvisioningBackend backend(
        {
            .config_root = root / "etc",
            .metadata_root = root / "etc/credentials",
            .key_root = root / "etc/keys",
            .lock_root = root / "run/locks",
            .udev_root = root / "udev",
            .systemd_root = root / "systemd",
            .public_root = root / "public",
        },
        root / "mnt",
        root / "mountinfo",
        root / "transactions",
        commands,
        btrfs,
        activator,
        credentials,
        safety,
        units
    );
    const auto candidate = backend.list_devices().front();
    commands.replace_on_scan = 3;
    int descriptors[2];
    test_helpers::expect_true("replacement secret pipe", ::pipe(descriptors) == 0, "cannot create pipe");
    constexpr std::string_view password = "secret";
    static_cast<void>(::write(descriptors[1], password.data(), password.size()));
    ::close(descriptors[1]);
    const auto started = backend.start(
        {
            .profile_id = "replacement",
            .profile_name = "Replacement",
            .candidate_id = "candidate-replacement",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
        },
        candidate,
        {.bus_name = ":1.6", .uid = 1000},
        descriptors[0]
    );
    ::close(descriptors[0]);
    const int helper_secret = secret_descriptor(password);
    backend.execute_operation(started.operation_id, helper_secret);
    ::close(helper_secret);
    const DevicePreparationStatus status = backend.status(started.operation_id);
    test_helpers::expect_eq("replacement state", status.state, "failed");
    test_helpers::expect_true(
        "replacement not wiped",
        std::ranges::find(commands.calls, std::string("wipefs"), [](const auto& call) { return call.front(); }) ==
            commands.calls.end(),
        "replacement device reached wipefs"
    );
}

void test_restart_marks_active_transaction_interrupted_and_preserves_owner() {
    const auto root = test_helpers::test_root("device-provisioning", "restart");
    const auto transaction_root = root / "transactions";
    DevicePreparationTransaction transaction;
    transaction.status.operation_id = "prepare-restored";
    transaction.status.profile_id = "test";
    transaction.status.state = "running";
    transaction.status.phase = "mkfs-btrfs";
    transaction.status.can_cancel = false;
    transaction.owner = {.bus_name = ":1.20", .uid = 1000};
    transaction.device.path = "/dev/test";
    transaction.device.major_minor = "8:16";
    transaction.profile_name = "Test";
    transaction.source_subvolume = "/home";
    transaction.passphrase_label = "Recovery";
    transaction.created_at = 100;
    transaction.updated_at = 100;
    transaction.last_completed_phase = "open";
    transaction.partition = "/dev/test1";
    transaction.luks_uuid = "11111111-2222-3333-4444-555555555555";
    transaction.mapper = "btrfs-backup-test";
    transaction.cleanup_result = "pending";
    DevicePreparationTransactionStore(transaction_root).save(transaction);

    Commands commands;
    Btrfs btrfs;
    Credentials credentials;
    SafetyInspector safety;
    Units units;
    config::NullConfigurationActivator activator;
    SystemDeviceProvisioningBackend backend(
        {
            .config_root = root / "etc",
            .metadata_root = root / "etc/credentials",
            .key_root = root / "etc/keys",
            .lock_root = root / "run/locks",
            .udev_root = root / "udev",
            .systemd_root = root / "systemd",
            .public_root = root / "public",
        },
        root / "mnt",
        root / "mountinfo",
        transaction_root,
        commands,
        btrfs,
        activator,
        credentials,
        safety,
        units
    );

    const DevicePreparationStatus restored = backend.status("prepare-restored");
    test_helpers::expect_eq("restart state", restored.state, "interrupted");
    test_helpers::expect_eq("restart error", restored.error_code, "device-preparation.daemon-restarted");
    test_helpers::expect_true("restart recovery", !restored.recovery_action.empty(), "recovery action missing");
    test_helpers::expect_true("cleanup helper launch", units.recoveries == 1, "cleanup helper was not launched");
    backend.recover_operation("prepare-restored");
    test_helpers::expect_true(
        "restored UID owner",
        backend.owned_by("prepare-restored", {.bus_name = ":1.99", .uid = 1000}),
        "same UID could not inspect restored operation"
    );
    test_helpers::expect_true(
        "foreign restored owner",
        !backend.owned_by("prepare-restored", {.bus_name = ":1.20", .uid = 1001}),
        "different UID owned restored operation"
    );
    test_helpers::expect_true(
        "restart mapper cleanup",
        std::ranges::find(commands.calls, std::vector<std::string>{"cryptsetup", "close", "btrfs-backup-test"}) !=
            commands.calls.end(),
        "restored mapper was not closed"
    );

    const auto persisted = DevicePreparationTransactionStore(transaction_root).load_and_prune();
    test_helpers::expect_true(
        "restart persisted",
        persisted.size() == 1 && persisted.front().status.state == "interrupted" &&
            persisted.front().mapper.empty() && persisted.front().cleanup_result == "mapper-closed",
        "restart recovery outcome was not persisted"
    );
}
} // namespace

int main() {
    test_preparation_sequence_uses_descriptors_and_installs_profile();
    test_replacement_before_wipe_is_rejected();
    test_exited_helper_marks_transaction_interrupted();
    test_restart_marks_active_transaction_interrupted_and_preserves_owner();
    return test_helpers::finish("system device provisioning backend tests");
}
