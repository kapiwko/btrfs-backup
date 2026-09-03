// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemDeviceProvisioningBackend.hpp>
#include <daemon/control/DevicePreparationTransaction.hpp>
#include <daemon/control/DevicePreparationUnitController.hpp>
#include <daemon/control/ExistingTargetInspector.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include <algorithm>
#include <backup/ports/ICommandRunner.hpp>
#include <backup/ports/IBtrfsOperations.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <daemon/control/CredentialAdministrationService.hpp>
#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>
#include <daemon/provisioning/StorageTopologyReader.hpp>
#include <platform/linux/storage/BlockDeviceMetadata.hpp>
#include <platform/linux/storage/BtrfsFilesystemFormatter.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/PartitionTableOperations.hpp>
#include <platform/linux/storage/SignatureOperations.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>

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
using btrfsbackup::daemon::control::IExistingTargetInspector;
using btrfsbackup::daemon::control::SystemDeviceProvisioningBackend;
using btrfsbackup::daemon::control::TargetCredential;

DevicePreparationTarget target(btrfsbackup::daemon::provisioning::StorageDevice device) {
    return {
        .device = std::move(device),
        .planned_partition_geometry = btrfsbackup::daemon::provisioning::PlannedPartitionGeometry{
            .start_sector = 1,
            .sector_count = 2047,
            .partition_number = 1,
        },
    };
}

DevicePreparationTarget partition_target(btrfsbackup::daemon::provisioning::StorageDevice device) {
    const auto* partition = std::get_if<btrfsbackup::daemon::provisioning::ExistingPartition>(
        &device.regions.front()
    );
    const auto selected_partition = *partition;
    return {
        .mode = btrfsbackup::daemon::provisioning::ProvisioningMode::ReformatExistingPartition,
        .device = std::move(device),
        .partition = selected_partition,
    };
}

DevicePreparationTarget adoption_target(btrfsbackup::daemon::provisioning::StorageDevice device) {
    auto* partition = std::get_if<btrfsbackup::daemon::provisioning::ExistingPartition>(
        &device.regions.front()
    );
    partition->filesystem = {
        .type = "crypto_LUKS",
        .uuid = "11111111-2222-3333-4444-555555555555",
    };
    partition->suitable_for_adoption = true;
    const auto selected_partition = *partition;
    return {
        .mode = btrfsbackup::daemon::provisioning::ProvisioningMode::AdoptExistingTarget,
        .device = std::move(device),
        .partition = selected_partition,
        .expected_inspection = btrfsbackup::daemon::provisioning::ExistingTargetInspectionSummary{
            .luks_uuid = "11111111-2222-3333-4444-555555555555",
            .btrfs_uuid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
            .partition_uuid = "99999999-8888-7777-6666-555555555555",
            .repository_id = "repository-1",
            .catalog_generation = 4,
            .snapshot_count = 2,
        },
    };
}

DevicePreparationTarget free_space_target(btrfsbackup::daemon::provisioning::StorageDevice device) {
    namespace provisioning = btrfsbackup::daemon::provisioning;
    device.partition_table = {.type = provisioning::PartitionTableType::Gpt, .identifier = "gpt-test"};
    provisioning::UnallocatedRegion free_region;
    free_region.start_sector = 2048;
    free_region.sector_count = 4096;
    free_region.suitable_for_backup_partition = true;
    return {
        .mode = provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace,
        .device = std::move(device),
        .free_region = std::move(free_region),
        .planned_partition_geometry = provisioning::PlannedPartitionGeometry{
            .start_sector = 2048,
            .sector_count = 4096,
            .partition_number = 2,
        },
    };
}

class Commands final : public backup::ICommandRunner {
  public:
    std::vector<std::vector<std::string>> calls;
    std::vector<std::vector<std::string>> controlled_calls;
    backup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
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

class MetadataReader final : public btrfsbackup::platform::linux::storage::IBlockDeviceMetadataReader {
  public:
    std::vector<std::string> calls;
    btrfsbackup::platform::linux::storage::BlockDeviceMetadata read(
        const std::filesystem::path& device
    ) override {
        calls.push_back(device.string());
        if (device == "/dev/mapper/btrfs-backup-test")
            return {.filesystem_uuid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"};
        return {.partition_uuid = "99999999-8888-7777-6666-555555555555"};
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
    std::string active_luks_uuid(const std::string&) override {
        return "11111111-2222-3333-4444-555555555555";
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
    void open_luks2_read_only(const std::filesystem::path& device, const std::string& mapper, int) override {
        calls.push_back("open-read-only:" + device.string() + ":" + mapper);
    }
    void close(const std::string& mapper) override {
        calls.push_back("close:" + mapper);
    }
};

class Signatures final : public btrfsbackup::platform::linux::storage::ISignatureOperations {
  public:
    const bool* required_partition_table_backup = nullptr;
    std::vector<std::pair<std::string, std::string>> calls;
    std::vector<std::optional<btrfsbackup::platform::linux::storage::SignatureExpectation>> expectations;
    void wipe_all(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::optional<btrfsbackup::platform::linux::storage::SignatureExpectation>& expected_signature
    ) override {
        if (required_partition_table_backup != nullptr && !*required_partition_table_backup)
            throw std::runtime_error("partition table was not backed up before signature erasure");
        calls.emplace_back(device.string(), expected_major_minor);
        expectations.push_back(expected_signature);
    }
};

class PartitionTables final : public btrfsbackup::platform::linux::storage::IPartitionTableOperations {
  public:
    bool partitioned = false;
    mutable bool snapshot_taken = false;
    mutable std::vector<btrfsbackup::platform::linux::storage::PartitionTableFormat> snapshot_formats;
    bool created_in_free_space = false;
    bool partition_creation_conflict = false;
    btrfsbackup::platform::linux::storage::PlannedPartitionGeometry created_geometry;
    std::vector<std::pair<std::string, std::string>> calls;
    std::string snapshot_partition_table(
        const std::filesystem::path&,
        const std::string&,
        btrfsbackup::platform::linux::storage::PartitionTableFormat format,
        const std::string&,
        std::uint32_t
    ) const override {
        snapshot_taken = true;
        snapshot_formats.push_back(format);
        return "label: gpt\nlabel-id: gpt-test\n";
    }
    btrfsbackup::platform::linux::storage::PlannedPartitionGeometry plan_partition_in_free_space(
        const std::filesystem::path&,
        const std::string&,
        const std::string&,
        std::uint32_t,
        std::uint64_t free_start_sector,
        std::uint64_t free_sector_count
    ) const override {
        return {
            .start_sector = free_start_sector,
            .sector_count = free_sector_count,
            .partition_number = 2,
        };
    }
    btrfsbackup::platform::linux::storage::PlannedPartitionGeometry plan_single_gpt_partition(
        const std::filesystem::path&,
        const std::string&,
        std::uint32_t
    ) const override {
        return {.start_sector = 1, .sector_count = 2047, .partition_number = 1};
    }
    btrfsbackup::platform::linux::storage::PartitionCreationInspection inspect_partition_creation(
        const std::filesystem::path&,
        const std::string&,
        const std::string&,
        std::uint32_t,
        std::uint64_t,
        std::uint64_t,
        const btrfsbackup::platform::linux::storage::PlannedPartitionGeometry& geometry
    ) const override {
        if (partition_creation_conflict)
            return {.state = btrfsbackup::platform::linux::storage::PartitionCreationState::Conflict};
        if (!created_in_free_space || geometry != created_geometry)
            return {.state = btrfsbackup::platform::linux::storage::PartitionCreationState::NotCreated};
        return {
            .state = btrfsbackup::platform::linux::storage::PartitionCreationState::Created,
            .partition = "/dev/test2",
        };
    }
    btrfsbackup::platform::linux::storage::PartitionCreationInspection inspect_single_gpt_partition(
        const std::filesystem::path&,
        const std::string&,
        std::uint32_t,
        const btrfsbackup::platform::linux::storage::PlannedPartitionGeometry& geometry
    ) const override {
        if (!partitioned || geometry != created_geometry)
            return {.state = btrfsbackup::platform::linux::storage::PartitionCreationState::Conflict};
        return {
            .state = btrfsbackup::platform::linux::storage::PartitionCreationState::Created,
            .partition = "/dev/test1",
        };
    }
    std::filesystem::path create_partition_in_free_space(
        const std::filesystem::path&,
        const std::string&,
        const std::string&,
        std::uint32_t,
        std::uint64_t,
        std::uint64_t,
        const btrfsbackup::platform::linux::storage::PlannedPartitionGeometry& geometry
    ) override {
        if (!snapshot_taken)
            throw std::runtime_error("partition table was not backed up");
        created_in_free_space = true;
        created_geometry = geometry;
        return "/dev/test2";
    }
    std::filesystem::path replace_with_single_gpt_partition(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const btrfsbackup::platform::linux::storage::PlannedPartitionGeometry& geometry
    ) override {
        calls.emplace_back(device.string(), expected_major_minor);
        created_geometry = geometry;
        partitioned = true;
        return "/dev/test1";
    }
};

class TopologyReader final : public btrfsbackup::daemon::provisioning::StorageTopologyReader {
  public:
    explicit TopologyReader(const PartitionTables& partition_tables) : partition_tables_(partition_tables) {
    }

    int scans = 0;
    int replace_on_scan = 0;
    std::optional<btrfsbackup::daemon::provisioning::PartitionTableType> partition_table_type_override;

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
        if (partition_tables_.partitioned) {
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
            partition.filesystem = {.type = "ext4", .uuid = "old-filesystem"};
            partition.suitable_for_reformat = true;
            device.regions.emplace_back(std::move(partition));
        }
        if (partition_table_type_override.has_value()) {
            device.partition_table.type = *partition_table_type_override;
            device.partition_table.identifier = "unsupported-test";
        }
        return {
            .generation = "test-generation-" + std::to_string(scans),
            .devices = {std::move(device)},
        };
    }

  private:
    const PartitionTables& partition_tables_;
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
    std::vector<std::string> inspect(
        const btrfsbackup::daemon::control::ProvisioningDevice&,
        const btrfsbackup::daemon::control::DevicePreparationTarget&
    ) const override {
        ++inspections;
        return reasons;
    }
};

class ExistingTargetInspector final : public IExistingTargetInspector {
  public:
    int inspections = 0;
    int cleanups = 0;
    btrfsbackup::daemon::provisioning::ExistingTargetInspectionSummary result;

    btrfsbackup::daemon::provisioning::ExistingTargetInspectionSummary inspect(
        const btrfsbackup::daemon::provisioning::ExistingPartition&,
        const std::string&,
        const std::filesystem::path&,
        int
    ) override {
        ++inspections;
        return result;
    }
    void cleanup_session(const std::string&, const std::filesystem::path&) override {
        ++cleanups;
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

void write_source_mountinfo(const std::filesystem::path& root) {
    test_helpers::write_file(
        root / "mountinfo",
        "21 31 0:20 / / rw,relatime - btrfs /dev/source-root rw,subvolid=5\n"
        "22 21 0:21 / /home rw,relatime - btrfs /dev/source-home rw,subvolid=5\n"
    );
}

std::string source_filesystem_uuid(const std::string& source) {
    if (source == "/dev/source-root")
        return "root-btrfs-uuid";
    if (source == "/dev/source-home")
        return "home-btrfs-uuid";
    if (source == "/dev/nested")
        return "nested-btrfs-uuid";
    return {};
}

void test_preparation_sequence_uses_descriptors_and_installs_profile() {
    const auto root = test_helpers::test_root("device-provisioning", "success");
    write_source_mountinfo(root);
    Commands commands;
    btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter btrfs_formatter(commands);
    Signatures signatures;
    MetadataReader metadata;
    PartitionTables partition_tables;
    signatures.required_partition_table_backup = &partition_tables.snapshot_taken;
    LuksOperations luks;
    TopologyReader topology(partition_tables);
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
        btrfs_formatter,
        signatures,
        metadata,
        partition_tables,
        luks,
        btrfs,
        activator,
        credentials,
        safety,
        units,
        true,
        nullptr,
        {},
        source_filesystem_uuid
    );
    const auto source_candidates = backend.list_source_candidates();
    test_helpers::expect_true(
        "source candidates retain filesystem identities",
        source_candidates.size() == 2 && source_candidates.at(0).path == "/" &&
            source_candidates.at(0).filesystem_uuid == "root-btrfs-uuid" &&
            source_candidates.at(0).local_snapshot_root == "/.snapshots/btrfs-backup" &&
            source_candidates.at(1).path == "/home" &&
            source_candidates.at(1).filesystem_uuid == "home-btrfs-uuid" &&
            source_candidates.at(1).local_snapshot_root == "/home/.snapshots/btrfs-backup",
        "source candidates did not distinguish root and separate Btrfs filesystems"
    );
    const auto reject_nested_source_mount = [&](const std::string& filesystem_type) {
        test_helpers::write_file(
            root / "mountinfo",
            "21 31 0:20 / / rw,relatime - btrfs /dev/source-root rw,subvolid=5\n"
            "22 21 0:21 / /home rw,relatime - btrfs /dev/source-home rw,subvolid=5\n"
            "23 22 0:22 / /home/.snapshots rw,relatime - " +
                filesystem_type +
                " /dev/nested rw\n"
        );
        const int invalid_secret = secret_descriptor("secret");
        bool rejected = false;
        try {
            static_cast<void>(backend.start(
                {
                    .profile_id = "invalid-source-root",
                    .profile_name = "Invalid source root",
                    .plan_id = "plan-invalid-source",
                    .source_subvolume = "/home",
                    .passphrase_label = "Recovery",
                },
                target(topology.scan().devices.front()),
                {.bus_name = ":1.4", .uid = 1000},
                invalid_secret
            ));
        } catch (const btrfsbackup::ValidationError&) {
            rejected = true;
        }
        ::close(invalid_secret);
        test_helpers::expect_true(
            "invalid local snapshot filesystem rejected",
            rejected && units.starts == 0,
            "local snapshot root on " + filesystem_type + " reached helper launch"
        );
    };
    reject_nested_source_mount("ext4");
    reject_nested_source_mount("btrfs");
    write_source_mountinfo(root);
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
    const auto reserved_by = DevicePreparationTransactionStore(root / "transactions")
                                 .profile_reservation_owner("test");
    test_helpers::expect_true(
        "profile identity reserved",
        reserved_by.has_value() && *reserved_by == started.operation_id,
        "profile identity was not durably reserved before helper launch"
    );
    const int duplicate_secret = secret_descriptor(password);
    bool duplicate_rejected = false;
    try {
        static_cast<void>(backend.start(
            {
                .profile_id = "test",
                .profile_name = "Duplicate",
                .plan_id = "plan-duplicate",
                .source_subvolume = "/home",
                .passphrase_label = "Recovery",
            },
            preparation_target,
            {.bus_name = ":1.6", .uid = 1000},
            duplicate_secret
        ));
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError& error) {
        duplicate_rejected = error.code() == btrfsbackup::daemon::dbus::ManagerErrorCode::Conflict;
    }
    ::close(duplicate_secret);
    test_helpers::expect_true(
        "duplicate profile reservation",
        duplicate_rejected && units.starts == 1,
        "second provisioning operation reserved the same profile identity"
    );
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
    const auto completed_transaction = DevicePreparationTransactionStore(root / "transactions")
                                           .load(started.operation_id);
    test_helpers::expect_true(
        "profile reservation released after commit",
        completed_transaction.profile_reservation_state == "released" &&
            !DevicePreparationTransactionStore(root / "transactions")
                 .profile_reservation_owner("test")
                 .has_value(),
        "completed provisioning retained its profile reservation"
    );
    const auto installed_profile = btrfsbackup::platform::linux::config::FileProfileRepository(
                                       root / "etc"
    )
                                       .get(ProfileId{"test"})
                                       .profile;
    test_helpers::expect_true(
        "local snapshots bound to source filesystem",
        completed_transaction.source_filesystem_uuid == "home-btrfs-uuid" &&
            completed_transaction.source_mount_root == "/home" &&
            completed_transaction.local_snapshot_dir == "/home/.snapshots/btrfs-backup/test" &&
            installed_profile.sources.front().local_snapshot_dir.value() ==
                std::filesystem::path("/home/.snapshots/btrfs-backup/test"),
        "profile local snapshot directory was not derived from the source Btrfs mount"
    );
    const int existing_secret = secret_descriptor(password);
    bool existing_rejected = false;
    try {
        static_cast<void>(backend.start(
            {
                .profile_id = "test",
                .profile_name = "Existing",
                .plan_id = "plan-existing",
                .source_subvolume = "/home",
                .passphrase_label = "Recovery",
            },
            preparation_target,
            {.bus_name = ":1.7", .uid = 1000},
            existing_secret
        ));
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError& error) {
        existing_rejected = error.code() == btrfsbackup::daemon::dbus::ManagerErrorCode::Conflict;
    }
    ::close(existing_secret);
    test_helpers::expect_true(
        "existing profile rejected before helper",
        existing_rejected && units.starts == 1,
        "existing profile identity reached helper launch"
    );
    const auto format = std::ranges::find(commands.calls, std::string("mkfs.btrfs"), [](const auto& call) { return call.front(); });
    test_helpers::expect_true(
        "destructive adapters",
        partition_tables.snapshot_formats ==
                std::vector<btrfsbackup::platform::linux::storage::PartitionTableFormat>{
                    btrfsbackup::platform::linux::storage::PartitionTableFormat::None,
                } &&
            signatures.calls == std::vector<std::pair<std::string, std::string>>{{"/dev/test", "8:16"}} &&
            partition_tables.calls ==
                std::vector<std::pair<std::string, std::string>>{{"/dev/test", "8:16"}} &&
            partition_tables.created_geometry ==
                btrfsbackup::platform::linux::storage::PlannedPartitionGeometry{
                    .start_sector = 1,
                    .sector_count = 2047,
                    .partition_number = 1,
                } &&
            format != commands.calls.end(),
        "device mutations did not use the expected adapters"
    );
    auto transaction = DevicePreparationTransactionStore(root / "transactions").load(started.operation_id);
    test_helpers::expect_eq(
        "whole-device partition table backup",
        transaction.partition_table_backup,
        "label: gpt\nlabel-id: gpt-test\n"
    );
    transaction.status.state = "interrupted";
    transaction.status.phase = "partition";
    transaction.last_completed_phase = "wipe-signatures";
    transaction.partition.clear();
    DevicePreparationTransactionStore(root / "transactions").save(transaction);
    backend.recover_operation(started.operation_id);
    const auto recovered = DevicePreparationTransactionStore(root / "transactions").load(started.operation_id);
    test_helpers::expect_true(
        "whole-device partition recovery",
        recovered.partition == "/dev/test1" && recovered.last_completed_phase == "partition" &&
            recovered.cleanup_result == "partition-detected" && !recovered.status.recovery_action.empty(),
        "recovery did not verify the replacement GPT and exact partition"
    );
    test_helpers::expect_true(
        "no sfdisk process",
        std::ranges::none_of(commands.calls, [](const auto& call) {
            return !call.empty() && call.front() == "sfdisk";
        }),
        "partition table mutation invoked sfdisk instead of libfdisk"
    );
    test_helpers::expect_true(
        "no wipefs process",
        std::ranges::none_of(commands.calls, [](const auto& call) {
            return !call.empty() && call.front() == "wipefs";
        }),
        "signature erasure invoked wipefs instead of libblkid"
    );
    test_helpers::expect_true(
        "no blkid process",
        std::ranges::none_of(commands.calls, [](const auto& call) {
            return !call.empty() && call.front() == "blkid";
        }) &&
            metadata.calls == std::vector<std::string>{
                                  "/dev/mapper/btrfs-backup-test",
                                  "/dev/test1",
                              },
        "device metadata did not use the libblkid adapter"
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
        "no udev settle process",
        std::ranges::none_of(commands.calls, [](const auto& call) {
            return !call.empty() && call.front() == "udevadm";
        }),
        "device preparation invoked udevadm instead of exact library verification"
    );
    test_helpers::expect_true(
        "library topology scan",
        std::ranges::none_of(commands.calls, [](const auto& call) {
            return !call.empty() && (call.front() == "lsblk" || (call.front() == "udevadm" && call.size() > 1 && call.at(1) == "info"));
        }),
        "device discovery invoked a command instead of the topology reader"
    );

    topology.partition_table_type_override =
        btrfsbackup::daemon::provisioning::PartitionTableType::Unsupported;
    const auto unsupported_target = target(topology.scan().devices.front());
    const int unsupported_manager_secret = secret_descriptor(password);
    const auto unsupported = backend.start(
        {
            .profile_id = "unsupported",
            .profile_name = "Unsupported table",
            .plan_id = "plan-unsupported",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
            .create_automatic_key = false,
        },
        unsupported_target,
        {.bus_name = ":1.5", .uid = 1000},
        unsupported_manager_secret
    );
    ::close(unsupported_manager_secret);
    const int unsupported_helper_secret = secret_descriptor(password);
    backend.execute_operation(unsupported.operation_id, unsupported_helper_secret);
    ::close(unsupported_helper_secret);
    test_helpers::expect_true(
        "unsupported partition table rejected before mutation",
        backend.status(unsupported.operation_id).state == "failed" &&
            partition_tables.snapshot_formats.size() == 1 && signatures.calls.size() == 1 &&
            partition_tables.calls.size() == 1,
        "unsupported partition table reached a destructive adapter"
    );
}

void test_existing_partition_does_not_modify_parent_partition_table() {
    const auto root = test_helpers::test_root("device-provisioning", "existing-partition");
    write_source_mountinfo(root);
    Commands commands;
    btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter btrfs_formatter(commands);
    Signatures signatures;
    MetadataReader metadata;
    PartitionTables partition_tables;
    partition_tables.partitioned = true;
    LuksOperations luks;
    TopologyReader topology(partition_tables);
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
        btrfs_formatter,
        signatures,
        metadata,
        partition_tables,
        luks,
        btrfs,
        activator,
        credentials,
        safety,
        units,
        true,
        nullptr,
        {},
        source_filesystem_uuid
    );
    const auto selected = partition_target(topology.scan().devices.front());
    const int manager_secret = secret_descriptor("secret");
    const auto started = backend.start(
        {
            .profile_id = "test",
            .profile_name = "Partition backup",
            .plan_id = "plan-partition",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
            .create_automatic_key = false,
        },
        selected,
        {.bus_name = ":1.6", .uid = 1000},
        manager_secret
    );
    ::close(manager_secret);
    const int helper_secret = secret_descriptor("secret");
    backend.execute_operation(started.operation_id, helper_secret);
    ::close(helper_secret);

    test_helpers::expect_eq("partition preparation state", backend.status(started.operation_id).state, "succeeded");
    test_helpers::expect_true(
        "partition-only signature wipe",
        signatures.calls == std::vector<std::pair<std::string, std::string>>{{"/dev/test1", "8:17"}} &&
            signatures.expectations ==
                std::vector<std::optional<btrfsbackup::platform::linux::storage::SignatureExpectation>>{
                    btrfsbackup::platform::linux::storage::SignatureExpectation{
                        .type = "ext4",
                        .version = {},
                        .label = {},
                        .uuid = "old-filesystem",
                    },
                },
        "signature wipe escaped the selected partition"
    );
    test_helpers::expect_true(
        "unchanged partition table",
        partition_tables.calls.empty(),
        "existing-partition preparation rewrote the parent partition table"
    );
    test_helpers::expect_true(
        "partition-only LUKS",
        luks.calls == std::vector<std::string>{
                          "format:/dev/test1",
                          "open:/dev/test1:btrfs-backup-test",
                          "close:btrfs-backup-test",
                      },
        "LUKS operations did not remain scoped to the selected partition"
    );
}

void test_free_space_preparation_uses_frozen_geometry() {
    const auto root = test_helpers::test_root("device-provisioning", "free-space");
    write_source_mountinfo(root);
    Commands commands;
    btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter btrfs_formatter(commands);
    Signatures signatures;
    MetadataReader metadata;
    PartitionTables partition_tables;
    LuksOperations luks;
    TopologyReader topology(partition_tables);
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
        btrfs_formatter,
        signatures,
        metadata,
        partition_tables,
        luks,
        btrfs,
        activator,
        credentials,
        safety,
        units,
        true,
        nullptr,
        {},
        source_filesystem_uuid
    );
    const auto selected = free_space_target(topology.scan().devices.front());
    const int manager_secret = secret_descriptor("secret");
    const auto started = backend.start(
        {
            .profile_id = "test",
            .profile_name = "Free-space backup",
            .plan_id = "plan-free-space",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
            .create_automatic_key = false,
        },
        selected,
        {.bus_name = ":1.7", .uid = 1000},
        manager_secret
    );
    ::close(manager_secret);
    const int helper_secret = secret_descriptor("secret");
    backend.execute_operation(started.operation_id, helper_secret);
    ::close(helper_secret);

    test_helpers::expect_true(
        "frozen free-space geometry",
        backend.status(started.operation_id).state == "succeeded" &&
            partition_tables.snapshot_taken && partition_tables.created_in_free_space &&
            partition_tables.created_geometry ==
                btrfsbackup::platform::linux::storage::PlannedPartitionGeometry{
                    .start_sector = 2048,
                    .sector_count = 4096,
                    .partition_number = 2,
                } &&
            signatures.calls.empty(),
        "free-space preparation changed scope or recomputed geometry"
    );
    auto transaction = DevicePreparationTransactionStore(root / "transactions").load(started.operation_id);
    test_helpers::expect_eq(
        "partition table backup",
        transaction.partition_table_backup,
        "label: gpt\nlabel-id: gpt-test\n"
    );
    test_helpers::expect_true(
        "new partition target",
        luks.calls == std::vector<std::string>{
                          "format:/dev/test2",
                          "open:/dev/test2:btrfs-backup-test",
                          "close:btrfs-backup-test",
                      },
        "LUKS was not limited to the newly verified partition"
    );

    transaction.status.state = "interrupted";
    transaction.status.phase = "partition";
    transaction.last_completed_phase = "backup-partition-table";
    transaction.partition.clear();
    DevicePreparationTransactionStore(root / "transactions").save(transaction);
    backend.recover_operation(started.operation_id);
    const auto recovered = DevicePreparationTransactionStore(root / "transactions").load(started.operation_id);
    test_helpers::expect_true(
        "created partition recovery",
        recovered.partition == "/dev/test2" && recovered.last_completed_phase == "partition" &&
            recovered.cleanup_result == "partition-detected" && !recovered.status.recovery_action.empty(),
        "recovery did not identify the partition created before interruption"
    );

    transaction = recovered;
    transaction.status.state = "interrupted";
    transaction.last_completed_phase = "backup-partition-table";
    transaction.partition.clear();
    partition_tables.created_in_free_space = false;
    DevicePreparationTransactionStore(root / "transactions").save(transaction);
    backend.recover_operation(started.operation_id);
    const auto not_created = DevicePreparationTransactionStore(root / "transactions").load(started.operation_id);
    test_helpers::expect_true(
        "missing partition recovery",
        not_created.partition.empty() && not_created.last_completed_phase == "backup-partition-table" &&
            not_created.cleanup_result == "partition-not-created" && !not_created.status.recovery_action.empty(),
        "recovery did not identify that the partition was not created"
    );

    transaction = not_created;
    transaction.status.state = "interrupted";
    transaction.cleanup_result = "not-required";
    partition_tables.partition_creation_conflict = true;
    DevicePreparationTransactionStore(root / "transactions").save(transaction);
    backend.recover_operation(started.operation_id);
    const auto conflicted = DevicePreparationTransactionStore(root / "transactions").load(started.operation_id);
    test_helpers::expect_true(
        "conflicting partition recovery",
        conflicted.partition.empty() && conflicted.cleanup_result == "partition-state-conflict" &&
            !conflicted.status.recovery_action.empty(),
        "recovery did not preserve an ambiguous partition state"
    );
}

void test_adoption_revalidates_fingerprint_without_modifying_target() {
    const auto root = test_helpers::test_root("device-provisioning", "adoption");
    write_source_mountinfo(root);
    Commands commands;
    btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter btrfs_formatter(commands);
    Signatures signatures;
    MetadataReader metadata;
    PartitionTables partition_tables;
    partition_tables.partitioned = true;
    LuksOperations luks;
    TopologyReader topology(partition_tables);
    Btrfs btrfs;
    Credentials credentials;
    SafetyInspector safety;
    ExistingTargetInspector inspector;
    Units units;
    config::NullConfigurationActivator activator;
    auto selected = adoption_target(topology.scan().devices.front());
    inspector.result = *selected.expected_inspection;
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
        btrfs_formatter,
        signatures,
        metadata,
        partition_tables,
        luks,
        btrfs,
        activator,
        credentials,
        safety,
        units,
        false,
        &inspector,
        root / "inspections",
        source_filesystem_uuid
    );
    const int manager_secret = secret_descriptor("secret");
    const auto started = backend.start(
        {
            .profile_id = "adopted",
            .profile_name = "Adopted backup",
            .plan_id = "plan-adoption",
            .source_subvolume = "/home",
            .passphrase_label = "Existing passphrase",
            .create_automatic_key = true,
        },
        selected,
        {.bus_name = ":1.8", .uid = 1000},
        manager_secret
    );
    ::close(manager_secret);
    const int helper_secret = secret_descriptor("secret");
    backend.execute_operation(started.operation_id, helper_secret);
    ::close(helper_secret);

    test_helpers::expect_eq("adoption state", backend.status(started.operation_id).state, "succeeded");
    test_helpers::expect_true("adoption reinspection", inspector.inspections == 1, "target was not reinspected");
    test_helpers::expect_true(
        "adoption does not mutate storage",
        signatures.calls.empty() && partition_tables.calls.empty() && luks.calls.empty() && commands.calls.empty(),
        "adoption invoked a target mutation"
    );
    test_helpers::expect_true(
        "adoption does not mutate credentials",
        !credentials.registered && !credentials.generated,
        "adoption registered or generated a credential"
    );
    test_helpers::expect_true(
        "adoption profile installed",
        std::filesystem::exists(root / "etc/profiles/adopted/profile.json"),
        "adopted profile was not installed"
    );
    const auto transaction = DevicePreparationTransactionStore(root / "transactions").load(started.operation_id);
    test_helpers::expect_true(
        "adoption transaction outcome",
        transaction.credentials_state == "not-applicable" &&
            transaction.luks_uuid == inspector.result.luks_uuid &&
            transaction.btrfs_uuid == inspector.result.btrfs_uuid &&
            !transaction.create_automatic_key,
        "adoption result was not persisted"
    );

    ++inspector.result.catalog_generation;
    const int changed_manager_secret = secret_descriptor("secret");
    const auto changed = backend.start(
        {
            .profile_id = "changed",
            .profile_name = "Changed backup",
            .plan_id = "plan-changed",
            .source_subvolume = "/home",
            .passphrase_label = "Existing passphrase",
        },
        selected,
        {.bus_name = ":1.8", .uid = 1000},
        changed_manager_secret
    );
    ::close(changed_manager_secret);
    const int changed_helper_secret = secret_descriptor("secret");
    backend.execute_operation(changed.operation_id, changed_helper_secret);
    ::close(changed_helper_secret);
    test_helpers::expect_eq("changed adoption state", backend.status(changed.operation_id).state, "failed");
    test_helpers::expect_true(
        "changed adoption not published",
        !std::filesystem::exists(root / "etc/profiles/changed/profile.json") && signatures.calls.empty() &&
            partition_tables.calls.empty() && luks.calls.empty() && commands.calls.empty(),
        "changed target was published or modified"
    );

    auto interrupted = DevicePreparationTransactionStore(root / "transactions").load(changed.operation_id);
    interrupted.status.state = "interrupted";
    interrupted.mapper = changed.operation_id;
    interrupted.inspection_mount_point = (root / "inspections" / changed.operation_id).string();
    interrupted.cleanup_result = "pending";
    std::filesystem::create_directories(interrupted.inspection_mount_point);
    DevicePreparationTransactionStore(root / "transactions").save(interrupted);
    backend.recover_operation(changed.operation_id);
    const auto recovered = DevicePreparationTransactionStore(root / "transactions").load(changed.operation_id);
    test_helpers::expect_true(
        "adoption recovery",
        inspector.cleanups == 1 && recovered.mapper.empty() && recovered.inspection_mount_point.empty() &&
            recovered.cleanup_result == "inspection-cleaned",
        "interrupted adoption session was not cleaned"
    );
}

void test_exited_helper_marks_transaction_interrupted() {
    const auto root = test_helpers::test_root("device-provisioning", "helper-exited");
    write_source_mountinfo(root);
    Commands commands;
    btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter btrfs_formatter(commands);
    Signatures signatures;
    MetadataReader metadata;
    PartitionTables partition_tables;
    LuksOperations luks;
    TopologyReader topology(partition_tables);
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
        btrfs_formatter,
        signatures,
        metadata,
        partition_tables,
        luks,
        btrfs,
        activator,
        credentials,
        safety,
        units,
        true,
        nullptr,
        {},
        source_filesystem_uuid
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
    write_source_mountinfo(root);
    Commands commands;
    btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter btrfs_formatter(commands);
    Signatures signatures;
    MetadataReader metadata;
    PartitionTables partition_tables;
    LuksOperations luks;
    TopologyReader topology(partition_tables);
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
        btrfs_formatter,
        signatures,
        metadata,
        partition_tables,
        luks,
        btrfs,
        activator,
        credentials,
        safety,
        units,
        true,
        nullptr,
        {},
        source_filesystem_uuid
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
    const auto failed_transaction = DevicePreparationTransactionStore(root / "transactions")
                                        .load(started.operation_id);
    test_helpers::expect_true(
        "safe pre-write failure releases profile reservation",
        failed_transaction.profile_reservation_state == "released" &&
            !DevicePreparationTransactionStore(root / "transactions")
                 .profile_reservation_owner("replacement")
                 .has_value(),
        "pre-write validation failure retained the profile reservation"
    );

    topology.replace_on_scan = 0;
    const auto source_replacement_target = target(topology.scan().devices.front());
    const int source_manager_secret = secret_descriptor(password);
    const auto source_started = backend.start(
        {
            .profile_id = "source-replacement",
            .profile_name = "Source replacement",
            .plan_id = "plan-source-replacement",
            .source_subvolume = "/home",
            .passphrase_label = "Recovery",
        },
        source_replacement_target,
        {.bus_name = ":1.7", .uid = 1000},
        source_manager_secret
    );
    ::close(source_manager_secret);
    test_helpers::write_file(
        root / "mountinfo",
        "21 31 0:20 / / rw,relatime - btrfs /dev/source-root rw,subvolid=5\n"
        "22 21 0:21 / /home rw,relatime - btrfs /dev/nested rw,subvolid=5\n"
    );
    const int source_helper_secret = secret_descriptor(password);
    backend.execute_operation(source_started.operation_id, source_helper_secret);
    ::close(source_helper_secret);
    test_helpers::expect_true(
        "source replacement rejected before wipe",
        backend.status(source_started.operation_id).state == "failed" && signatures.calls.empty(),
        "changed source Btrfs identity reached a destructive adapter"
    );
}

void test_restart_marks_active_transaction_interrupted_and_preserves_owner() {
    const auto root = test_helpers::test_root("device-provisioning", "restart");
    write_source_mountinfo(root);
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
    transaction.target.planned_partition_geometry =
        btrfsbackup::daemon::provisioning::PlannedPartitionGeometry{
            .start_sector = 1,
            .sector_count = 2047,
            .partition_number = 1,
        };
    transaction.profile_name = "Test";
    transaction.source_subvolume = "/home";
    transaction.source_filesystem_uuid = "home-btrfs-uuid";
    transaction.source_mount_root = "/home";
    transaction.local_snapshot_dir = "/home/.snapshots/btrfs-backup/test";
    transaction.passphrase_label = "Recovery";
    transaction.created_at = 100;
    transaction.updated_at = 100;
    transaction.last_completed_phase = "open";
    transaction.partition = "/dev/test1";
    transaction.luks_uuid = "11111111-2222-3333-4444-555555555555";
    transaction.mapper = "btrfs-backup-test";
    transaction.cleanup_result = "pending";
    transaction.profile_reservation_state = "held";
    DevicePreparationTransactionStore(transaction_root).reserve_profile("test", "prepare-restored");
    DevicePreparationTransactionStore(transaction_root).save(transaction);

    Commands commands;
    btrfsbackup::platform::linux::storage::CommandBtrfsFilesystemFormatter btrfs_formatter(commands);
    Signatures signatures;
    MetadataReader metadata;
    PartitionTables partition_tables;
    LuksOperations luks;
    TopologyReader topology(partition_tables);
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
        btrfs_formatter,
        signatures,
        metadata,
        partition_tables,
        luks,
        btrfs,
        activator,
        credentials,
        safety,
        units,
        true,
        nullptr,
        {},
        source_filesystem_uuid
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
            persisted.front().mapper.empty() && persisted.front().cleanup_result == "mapper-closed" &&
            persisted.front().profile_reservation_state == "held" &&
            DevicePreparationTransactionStore(transaction_root)
                    .profile_reservation_owner("test") == std::optional<std::string>{"prepare-restored"},
        "restart recovery outcome was not persisted"
    );
}
} // namespace

int main() {
    test_preparation_sequence_uses_descriptors_and_installs_profile();
    test_existing_partition_does_not_modify_parent_partition_table();
    test_free_space_preparation_uses_frozen_geometry();
    test_adoption_revalidates_fingerprint_without_modifying_target();
    test_replacement_before_wipe_is_rejected();
    test_exited_helper_marks_transaction_interrupted();
    test_restart_marks_active_transaction_interrupted_and_preserves_owner();
    return test_helpers::finish("system device provisioning backend tests");
}
