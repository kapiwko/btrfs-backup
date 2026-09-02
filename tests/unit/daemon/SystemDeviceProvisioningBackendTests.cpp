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
#include <daemon/provisioning/StorageTopologyReader.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/SignatureOperations.hpp>

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
using btrfsbackup::daemon::control::DevicePreparationTarget;
using btrfsbackup::daemon::control::DevicePreparationTransaction;
using btrfsbackup::daemon::control::DevicePreparationTransactionStore;
using btrfsbackup::daemon::control::ICredentialAdministrationBackend;
using btrfsbackup::daemon::control::IDestructiveDeviceSafetyInspector;
using btrfsbackup::daemon::control::IDevicePreparationUnitController;
using btrfsbackup::daemon::control::SystemDeviceProvisioningBackend;
using btrfsbackup::daemon::control::TargetCredential;

DevicePreparationTarget target(btrfsbackup::daemon::provisioning::StorageDevice device) {
    return {.device = std::move(device)};
}

class Commands final : public backup::ICommandRunner {
  public:
    std::vector<std::vector<std::string>> calls;
    std::vector<std::vector<std::string>> controlled_calls;
    backup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
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

class LuksOperations final : public btrfsbackup::platform::linux::storage::ICryptsetupOperations {
  public:
    std::vector<std::string> calls;
    btrfsbackup::platform::linux::storage::LuksHeader inspect_luks2(const std::filesystem::path&) override {
        return {"11111111-2222-3333-4444-555555555555", {0}};
    }
    void add_key(const std::filesystem::path&, int, int) override {
    }
    void test_key(const std::filesystem::path&, int) override {
    }
    void remove_keyslot(const std::filesystem::path&, int, int) override {
    }
    std::filesystem::path active_device(const std::string&) override {
        return "/dev/test";
    }
    std::string format_luks2(const std::filesystem::path& device, int) override {
        calls.push_back("format:" + device.string());
        return "11111111-2222-3333-4444-555555555555";
    }
    void open_luks2(const std::filesystem::path& device, const std::string& mapper, int) override {
        calls.push_back("open:" + device.string() + ":" + mapper);
    }
    void close(const std::string& mapper) override {
        calls.push_back("close:" + mapper);
    }
};

class Signatures final : public btrfsbackup::platform::linux::storage::ISignatureOperations {
  public:
    std::vector<std::pair<std::string, std::string>> calls;
    void wipe_all(const std::filesystem::path& device, const std::string& expected_major_minor) override {
        calls.emplace_back(device.string(), expected_major_minor);
    }
};

class TopologyReader final : public btrfsbackup::daemon::provisioning::StorageTopologyReader {
  public:
    explicit TopologyReader(const Commands& commands) : commands_(commands) {
    }

    int scans = 0;
    int replace_on_scan = 0;

    btrfsbackup::daemon::provisioning::StorageTopology scan() override {
        namespace provisioning = btrfsbackup::daemon::provisioning;
        ++scans;
        const bool replaced = replace_on_scan != 0 && scans >= replace_on_scan;
        provisioning::StorageDevice device;
        device.identity = {
            .display_path = "/dev/test",
            .major_minor = "8:16",
            .sysfs_path = replaced ? "/sys/devices/other/block/test" : "/sys/devices/test/block/test",
            .wwn = replaced ? "WWN-OTHER" : "WWN-TEST",
            .serial = replaced ? "OTHER" : "VENDOR_SERIAL",
            .serial_short = replaced ? "OTHER" : "SERIAL",
            .size_bytes = 1048576,
        };
        device.display_name = replaced ? "Other" : "Test";
        device.transport = "usb";
        device.size_bytes = 1048576;
        device.logical_sector_size = 512;
        device.physical_sector_size = 4096;
        device.removable = true;
        const bool partitioned = std::ranges::any_of(commands_.calls, [](const auto& call) {
            return !call.empty() && call.front() == "sfdisk";
        });
        if (partitioned) {
            device.partition_table = {
                .type = provisioning::PartitionTableType::Gpt,
                .identifier = "gpt-test",
            };
            provisioning::ExistingPartition partition;
            partition.identity = {
                .display_path = "/dev/test1",
                .major_minor = "8:17",
                .sysfs_path = "/sys/devices/test/block/test/test1",
                .size_bytes = 1048064,
            };
            partition.partition_uuid = "99999999-8888-7777-6666-555555555555";
            partition.partition_number = 1;
            partition.start_sector = 1;
            partition.sector_count = 2047;
            device.regions.emplace_back(std::move(partition));
        }
        return {
            .generation = "test-generation-" + std::to_string(scans),
            .devices = {std::move(device)},
        };
    }

  private:
    const Commands& commands_;
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
    Signatures signatures;
    LuksOperations luks;
    TopologyReader topology(commands);
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
        topology,
        commands,
        signatures,
        luks,
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
    const auto preparation_target = target(topology.scan().devices.front());
    const auto started = backend.start(
        {
            .profile_id = "test",
            .profile_name = "Test backup",
            .plan_id = "plan-test",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
            .create_automatic_key = true,
        },
        preparation_target,
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
    const auto partition = std::ranges::find(commands.calls, std::string("sfdisk"), [](const auto& call) { return call.front(); });
    const auto format = std::ranges::find(commands.calls, std::string("mkfs.btrfs"), [](const auto& call) { return call.front(); });
    test_helpers::expect_true(
        "destructive command order",
        signatures.calls == std::vector<std::pair<std::string, std::string>>{{"/dev/test", "8:16"}} &&
            partition != commands.calls.end() && format != commands.calls.end() && partition < format,
        "device commands ran out of order"
    );
    test_helpers::expect_true(
        "no wipefs process",
        std::ranges::none_of(commands.calls, [](const auto& call) {
            return !call.empty() && call.front() == "wipefs";
        }),
        "signature erasure invoked wipefs instead of libblkid"
    );
    test_helpers::expect_true(
        "no cryptsetup process",
        std::ranges::none_of(commands.calls, [](const auto& call) {
            return !call.empty() && call.front() == "cryptsetup";
        }) &&
            luks.calls == std::vector<std::string>{
                              "format:/dev/test1",
                              "open:/dev/test1:btrfs-backup-test",
                              "close:btrfs-backup-test",
                          },
        "LUKS provisioning did not use the libcryptsetup adapter"
    );
    test_helpers::expect_true(
        "identity checked immediately before wipe",
        topology.scans >= 3,
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
    test_helpers::expect_true(
        "library topology scan",
        std::ranges::none_of(commands.calls, [](const auto& call) {
            return !call.empty() && (call.front() == "lsblk" || (call.front() == "udevadm" && call.size() > 1 && call.at(1) == "info"));
        }),
        "device discovery invoked a command instead of the topology reader"
    );
}

void test_exited_helper_marks_transaction_interrupted() {
    const auto root = test_helpers::test_root("device-provisioning", "helper-exited");
    Commands commands;
    Signatures signatures;
    LuksOperations luks;
    TopologyReader topology(commands);
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
        topology,
        commands,
        signatures,
        luks,
        btrfs,
        activator,
        credentials,
        safety,
        units
    );
    const auto preparation_target = target(topology.scan().devices.front());
    const int secret = secret_descriptor("secret");
    const auto started = backend.start(
        {
            .profile_id = "helper-exited",
            .profile_name = "Helper exited",
            .plan_id = "plan-helper-exited",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
        },
        preparation_target,
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
    Signatures signatures;
    LuksOperations luks;
    TopologyReader topology(commands);
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
        topology,
        commands,
        signatures,
        luks,
        btrfs,
        activator,
        credentials,
        safety,
        units
    );
    const auto preparation_target = target(topology.scan().devices.front());
    topology.replace_on_scan = 3;
    int descriptors[2];
    test_helpers::expect_true("replacement secret pipe", ::pipe(descriptors) == 0, "cannot create pipe");
    constexpr std::string_view password = "secret";
    static_cast<void>(::write(descriptors[1], password.data(), password.size()));
    ::close(descriptors[1]);
    const auto started = backend.start(
        {
            .profile_id = "replacement",
            .profile_name = "Replacement",
            .plan_id = "plan-replacement",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
        },
        preparation_target,
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
        signatures.calls.empty(),
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
    transaction.target.device.identity = {
        .display_path = "/dev/test",
        .major_minor = "8:16",
        .sysfs_path = "/sys/devices/test/block/test",
        .wwn = "WWN-TEST",
        .serial = "VENDOR_SERIAL",
        .serial_short = "SERIAL",
        .size_bytes = 1048576,
    };
    transaction.target.device.size_bytes = 1048576;
    transaction.target.device.transport = "usb";
    transaction.target.device.logical_sector_size = 512;
    transaction.target.device.physical_sector_size = 4096;
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
    Signatures signatures;
    LuksOperations luks;
    TopologyReader topology(commands);
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
        topology,
        commands,
        signatures,
        luks,
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
        luks.calls == std::vector<std::string>{"close:btrfs-backup-test"},
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
