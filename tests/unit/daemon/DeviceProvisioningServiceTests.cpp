// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DeviceProvisioningService.hpp>

#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <daemon/provisioning/StorageTopologyReader.hpp>

#include "support/TestHelpers.hpp"

namespace {
using btrfsbackup::daemon::control::DevicePreparationRequest;
using btrfsbackup::daemon::control::DevicePreparationStatus;
using btrfsbackup::daemon::control::DeviceProvisioningService;
using btrfsbackup::daemon::control::IDeviceProvisioningBackend;
using btrfsbackup::daemon::control::IManagerAuthorizer;
using btrfsbackup::daemon::control::ManagerAuthorizationAction;
using btrfsbackup::daemon::provisioning::ExistingPartition;
using btrfsbackup::daemon::provisioning::PartitionTableType;
using btrfsbackup::daemon::provisioning::ProvisioningMode;
using btrfsbackup::daemon::provisioning::StorageDevice;
using btrfsbackup::daemon::provisioning::StorageRegion;
using btrfsbackup::daemon::provisioning::StorageTopology;
using btrfsbackup::daemon::provisioning::StorageTopologyReader;
using btrfsbackup::daemon::provisioning::UnallocatedRegion;

class Authorizer final : public IManagerAuthorizer {
  public:
    bool allowed = true;
    bool active = true;
    std::vector<ManagerAuthorizationAction> actions;
    std::vector<std::string>* events = nullptr;
    bool authorize(const std::string&, ManagerAuthorizationAction action) override {
        if (events != nullptr)
            events->push_back("authorize");
        actions.push_back(action);
        return allowed && action == ManagerAuthorizationAction::PrepareBackupDevice;
    }
    bool caller_is_active(const std::string&) override {
        return active;
    }
};

class Backend final : public IDeviceProvisioningBackend {
  public:
    int received_fd = -1;
    int starts = 0;
    mutable int geometry_plans = 0;
    bool cancelled = false;
    std::vector<std::string> safety_reasons;
    std::vector<std::string>* events = nullptr;
    btrfsbackup::daemon::control::DevicePreparationOwner owner;
    btrfsbackup::daemon::control::DevicePreparationTarget target;
    btrfsbackup::daemon::provisioning::ExistingTargetClassification inspection_classification =
        btrfsbackup::daemon::provisioning::ExistingTargetClassification::CompatibleRepository;
    std::vector<std::string> list_source_candidates() override {
        return {"/home"};
    }
    std::vector<std::string> inspect_safety(
        const btrfsbackup::daemon::control::DevicePreparationTarget&
    ) const override {
        if (events != nullptr)
            events->push_back("inspect");
        return safety_reasons;
    }
    btrfsbackup::daemon::provisioning::PlannedPartitionGeometry plan_partition_geometry(
        const btrfsbackup::daemon::provisioning::StorageDevice&,
        const btrfsbackup::daemon::provisioning::UnallocatedRegion& free_region
    ) const override {
        ++geometry_plans;
        return {
            .start_sector = free_region.start_sector + 1,
            .sector_count = free_region.sector_count - 2,
            .partition_number = 2,
        };
    }
    btrfsbackup::daemon::provisioning::PlannedPartitionGeometry plan_whole_device_partition_geometry(
        const btrfsbackup::daemon::provisioning::StorageDevice&
    ) const override {
        ++geometry_plans;
        return {.start_sector = 1, .sector_count = 14, .partition_number = 1};
    }
    btrfsbackup::daemon::provisioning::ExistingTargetInspectionSummary inspect_existing_target(
        const btrfsbackup::daemon::control::DevicePreparationTarget& received_target,
        int passphrase_fd
    ) override {
        target = received_target;
        received_fd = passphrase_fd;
        return {
            .classification = inspection_classification,
            .diagnostic_code = inspection_classification ==
                    btrfsbackup::daemon::provisioning::ExistingTargetClassification::CompatibleRepository
                ? ""
                : "repository-not-found",
            .luks_uuid = "luks-uuid",
            .btrfs_uuid = "btrfs-uuid",
            .partition_uuid = "part-uuid",
            .repository_id = inspection_classification ==
                    btrfsbackup::daemon::provisioning::ExistingTargetClassification::CompatibleRepository
                ? "repository-1"
                : "",
            .catalog_generation = 7,
            .snapshot_count = 2,
        };
    }
    DevicePreparationStatus start(
        const DevicePreparationRequest& request,
        const btrfsbackup::daemon::control::DevicePreparationTarget& received_target,
        const btrfsbackup::daemon::control::DevicePreparationOwner& received_owner,
        int passphrase_fd
    ) override {
        owner = received_owner;
        target = received_target;
        received_fd = passphrase_fd;
        ++starts;
        test_helpers::expect_eq("resolved target path", received_target.device.identity.display_path, "/dev/test");
        return {.operation_id = "prepare-1", .profile_id = request.profile_id, .state = "queued", .phase = "inspect", .can_cancel = true};
    }
    DevicePreparationStatus status(const std::string&) const override {
        return {.operation_id = "prepare-1", .profile_id = "test", .state = "running", .phase = "partition"};
    }
    bool owned_by(
        const std::string&,
        const btrfsbackup::daemon::control::DevicePreparationOwner& candidate
    ) const override {
        return candidate.bus_name == owner.bus_name && candidate.uid == owner.uid;
    }
    void cancel(const std::string&) override {
        cancelled = true;
    }
};

class TopologyReader final : public StorageTopologyReader {
  public:
    std::string generation = "topology-1";
    bool partition_mounted = false;
    StorageTopology scan() override {
        ExistingPartition partition{
            .candidate_id = "raw-partition",
            .identity = {.display_path = "/dev/test1", .major_minor = "8:17", .size_bytes = 512},
            .partition_uuid = "part-uuid",
            .partition_number = 1,
            .start_sector = 1,
            .sector_count = 1,
            .filesystem = {.type = "crypto_LUKS", .uuid = "luks-uuid"},
            .suitable_for_reformat = true,
            .suitable_for_adoption = true,
        };
        if (partition_mounted) {
            partition.mount_points = {"/media/target"};
            partition.blockers = {{"mounted-filesystem", "/media/target"}};
        }
        StorageDevice device{
            .candidate_id = "raw-device",
            .identity = {
                .display_path = "/dev/test",
                .major_minor = "8:16",
                .sysfs_path = "/devices/test/block/test",
                .wwn = "wwn-test",
                .serial = "vendor_serial",
                .serial_short = "serial",
                .size_bytes = 8192,
            },
            .display_name = "Test disk",
            .transport = "usb",
            .size_bytes = 8192,
            .logical_sector_size = 512,
            .partition_table = {.type = PartitionTableType::Gpt, .identifier = "pt-uuid"},
            .regions = {
                StorageRegion{std::move(partition)},
                StorageRegion{UnallocatedRegion{
                    .id = "raw-free",
                    .start_sector = 4,
                    .sector_count = 8,
                    .suitable_for_backup_partition = true,
                }},
            },
        };
        return {.generation = generation, .devices = {std::move(device)}};
    }
};

DevicePreparationRequest request(std::string plan_id = "plan-1") {
    return {
        .profile_id = "test",
        .profile_name = "Test",
        .plan_id = std::move(plan_id),
        .source_subvolume = "/home",
        .passphrase_label = "Recovery",
        .create_automatic_key = true,
    };
}

DevicePreparationRequest plan_request(std::string plan_id) {
    return request(std::move(plan_id));
}

void test_read_only_discovery_requires_only_an_active_caller() {
    Authorizer authorizer;
    authorizer.allowed = false;
    Backend backend;
    TopologyReader reader;
    DeviceProvisioningService service(authorizer, backend, std::chrono::minutes(5), {}, {}, &reader);
    static_cast<void>(service.inspect_storage_topology(":1.6"));
    static_cast<void>(service.list_source_candidates(":1.6"));
    test_helpers::expect_true("read-only authorization", authorizer.actions.empty(), "read-only discovery invoked polkit");

    authorizer.active = false;
    try {
        static_cast<void>(service.inspect_storage_topology(":1.7"));
        test_helpers::fail("inactive topology inspection", "inspection was accepted");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {}
}

void test_invalid_request_is_rejected_before_backend() {
    Authorizer authorizer;
    Backend backend;
    DeviceProvisioningService service(authorizer, backend);
    auto invalid = request();
    invalid.source_subvolume = "relative/source";
    try {
        static_cast<void>(service.start(":1.5", 1000, invalid, 17));
        test_helpers::fail("invalid preparation", "relative source was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
    try {
        static_cast<void>(service.start(":1.5", 1000, request(), -1));
        test_helpers::fail("invalid descriptor", "invalid descriptor was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
}

void test_topology_and_plan_are_caller_bound_and_revalidated() {
    Authorizer authorizer;
    Backend backend;
    std::vector<std::string> events;
    authorizer.events = &events;
    backend.events = &events;
    TopologyReader reader;
    int sequence = 0;
    DeviceProvisioningService service(
        authorizer,
        backend,
        std::chrono::minutes(5),
        [&] { return "opaque-" + std::to_string(++sequence); },
        {},
        &reader
    );
    const auto topology = service.inspect_storage_topology(":1.20");
    test_helpers::expect_true(
        "opaque topology candidates",
        topology.devices.front().candidate_id != "raw-device" &&
            std::get<ExistingPartition>(topology.devices.front().regions.front()).candidate_id != "raw-partition",
        "raw storage identity escaped as a candidate"
    );
    const auto& partition = std::get<ExistingPartition>(topology.devices.front().regions.front());
    const auto partition_plan = service.build_device_preparation_plan(
        ":1.20",
        topology.generation,
        partition.candidate_id,
        ProvisioningMode::ReformatExistingPartition
    );
    static_cast<void>(service.start(":1.20", 1000, plan_request(partition_plan.id), 17));
    test_helpers::expect_true(
        "partition execution target",
        backend.target.mode == ProvisioningMode::ReformatExistingPartition && backend.target.partition.has_value() &&
            backend.target.partition->identity.display_path == "/dev/test1",
        "partition plan did not reach the backend with its exact target"
    );
    const auto plan = service.build_device_preparation_plan(
        ":1.20",
        topology.generation,
        topology.devices.front().candidate_id,
        ProvisioningMode::EraseWholeDevice
    );
    test_helpers::expect_true(
        "whole-device geometry plan",
        !plan.id.empty() && plan.after.regions.size() == 1 && plan.after.regions.front().geometry_exact &&
            plan.after.regions.front().start_sector == 1 && plan.after.regions.front().sector_count == 14 &&
            plan.after.regions.front().partition_number == 1,
        "whole-device plan did not expose exact backend geometry"
    );
    try {
        static_cast<void>(service.start(":1.21", 1001, plan_request(plan.id), 17));
        test_helpers::fail("foreign plan", "another caller started a preparation plan");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    events.clear();
    const auto started = service.start(
        ":1.20",
        1000,
        plan_request(plan.id),
        17
    );
    test_helpers::expect_true(
        "safety before authorization",
        events == std::vector<std::string>{"inspect", "authorize"},
        "device safety was not inspected before polkit"
    );
    test_helpers::expect_eq("topology execution candidate", started.operation_id, "prepare-1");
    test_helpers::expect_true(
        "whole-device execution geometry",
        backend.target.mode == ProvisioningMode::EraseWholeDevice &&
            backend.target.planned_partition_geometry.has_value() &&
            backend.target.planned_partition_geometry->start_sector == 1 &&
            backend.target.planned_partition_geometry->sector_count == 14 &&
            backend.target.planned_partition_geometry->partition_number == 1,
        "whole-device geometry changed between plan and execution"
    );
    test_helpers::expect_true("secret descriptor", backend.received_fd == 17, "descriptor not forwarded");
    authorizer.allowed = false;
    test_helpers::expect_eq(
        "owner status",
        service.status(":1.20", 1000, started.operation_id).operation_id,
        started.operation_id
    );
    try {
        static_cast<void>(service.status(":1.21", 1001, started.operation_id));
        test_helpers::fail("foreign status", "status was disclosed");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    authorizer.allowed = true;
    service.cancel(":1.20", 1000, started.operation_id);
    test_helpers::expect_true("cancel", backend.cancelled, "cancel not forwarded");
    try {
        static_cast<void>(service.start(":1.20", 1000, plan_request(plan.id), 17));
        test_helpers::fail("reused plan", "a preparation plan was reusable");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    try {
        static_cast<void>(service.build_device_preparation_plan(
            ":1.21",
            topology.generation,
            topology.devices.front().candidate_id,
            ProvisioningMode::EraseWholeDevice
        ));
        test_helpers::fail("foreign topology", "another caller reused a topology snapshot");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    const auto refreshed_topology = service.inspect_storage_topology(":1.20");
    const auto stale_plan = service.build_device_preparation_plan(
        ":1.20",
        refreshed_topology.generation,
        refreshed_topology.devices.front().candidate_id,
        ProvisioningMode::EraseWholeDevice
    );
    reader.generation = "topology-2";
    try {
        static_cast<void>(service.start(":1.20", 1000, plan_request(stale_plan.id), 17));
        test_helpers::fail("changed topology", "a plan for changed topology was started");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    const auto safety_topology = service.inspect_storage_topology(":1.20");
    const auto unsafe_plan = service.build_device_preparation_plan(
        ":1.20",
        safety_topology.generation,
        safety_topology.devices.front().candidate_id,
        ProvisioningMode::EraseWholeDevice
    );
    reader.partition_mounted = true;
    try {
        static_cast<void>(service.start(":1.20", 1000, plan_request(unsafe_plan.id), 17));
        test_helpers::fail("changed safety state", "mounted child was accepted without a generation change");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
    reader.partition_mounted = false;
    const auto latest_topology = service.inspect_storage_topology(":1.20");
    const auto replaced_plan = service.build_device_preparation_plan(
        ":1.20",
        latest_topology.generation,
        latest_topology.devices.front().candidate_id,
        ProvisioningMode::EraseWholeDevice
    );
    static_cast<void>(service.inspect_storage_topology(":1.20"));
    try {
        static_cast<void>(service.start(":1.20", 1000, plan_request(replaced_plan.id), 17));
        test_helpers::fail("replaced plan", "a plan survived a newer topology inspection");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {
    }
}

void test_existing_target_inspection_is_caller_bound_and_invalidated_by_rescan() {
    Authorizer authorizer;
    Backend backend;
    TopologyReader reader;
    int sequence = 0;
    DeviceProvisioningService service(
        authorizer,
        backend,
        std::chrono::minutes(5),
        [&] { return "inspection-" + std::to_string(++sequence); },
        {},
        &reader
    );
    const auto topology = service.inspect_storage_topology(":1.30");
    const auto& partition = std::get<ExistingPartition>(topology.devices.front().regions.front());
    const auto inspection = service.inspect_existing_target(
        ":1.30",
        topology.generation,
        partition.candidate_id,
        23
    );
    test_helpers::expect_true("inspection id", !inspection.inspection_id.empty(), "inspection ID is empty");
    test_helpers::expect_eq("inspection repository", inspection.target.repository_id, "repository-1");
    test_helpers::expect_true("inspection descriptor", backend.received_fd == 23, "credential descriptor was not forwarded");
    test_helpers::expect_true(
        "inspection target",
        backend.target.mode == ProvisioningMode::AdoptExistingTarget && backend.target.partition.has_value(),
        "adoption target did not reach backend"
    );
    const auto plan = service.build_device_preparation_plan(
        ":1.30",
        topology.generation,
        partition.candidate_id,
        ProvisioningMode::AdoptExistingTarget,
        inspection.inspection_id
    );
    test_helpers::expect_true(
        "inspection-bound adoption plan",
        plan.mode == ProvisioningMode::AdoptExistingTarget &&
            plan.inspection_id == std::optional<std::string>{inspection.inspection_id} &&
            plan.destructive_scope.kind() == btrfsbackup::daemon::provisioning::DestructiveScopeKind::None &&
            plan.before == plan.after,
        "adoption plan is not bound to the read-only inspection"
    );
    static_cast<void>(service.start(":1.30", 1000, plan_request(plan.id), 23));
    test_helpers::expect_true(
        "adoption execution target",
        backend.starts == 1 && backend.target.expected_inspection.has_value() &&
            backend.target.expected_inspection->repository_id == "repository-1" &&
            backend.target.expected_inspection->catalog_generation == 7,
        "accepted inspection fingerprint did not reach the backend"
    );
    try {
        static_cast<void>(service.build_device_preparation_plan(
            ":1.30",
            topology.generation,
            partition.candidate_id,
            ProvisioningMode::AdoptExistingTarget,
            inspection.inspection_id
        ));
        test_helpers::fail("consumed inspection", "an inspection was reusable after starting its plan");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {}
    authorizer.allowed = false;
    try {
        static_cast<void>(service.inspect_existing_target(
            ":1.31",
            topology.generation,
            partition.candidate_id,
            23
        ));
        test_helpers::fail("foreign inspection", "another caller inspected a topology candidate");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {}
    authorizer.allowed = true;
    static_cast<void>(service.inspect_storage_topology(":1.30"));
}

void test_non_adoptable_inspection_has_no_reusable_token() {
    Authorizer authorizer;
    Backend backend;
    backend.inspection_classification =
        btrfsbackup::daemon::provisioning::ExistingTargetClassification::EmptyFilesystem;
    TopologyReader reader;
    DeviceProvisioningService service(authorizer, backend, std::chrono::minutes(5), {}, {}, &reader);
    const auto topology = service.inspect_storage_topology(":1.40");
    const auto& partition = std::get<ExistingPartition>(topology.devices.front().regions.front());
    const auto inspection = service.inspect_existing_target(
        ":1.40",
        topology.generation,
        partition.candidate_id,
        23
    );
    test_helpers::expect_true(
        "non-adoptable inspection",
        inspection.inspection_id.empty() && !inspection.target.adoptable(),
        "non-adoptable target received a reusable inspection token"
    );
    try {
        static_cast<void>(service.build_device_preparation_plan(
            ":1.40",
            topology.generation,
            partition.candidate_id,
            ProvisioningMode::AdoptExistingTarget,
            inspection.inspection_id
        ));
        test_helpers::fail("non-adoptable plan", "an empty target produced an adoption plan");
    } catch (const btrfsbackup::daemon::dbus::ManagerOperationError&) {}
}

void test_free_space_plan_uses_backend_geometry() {
    Authorizer authorizer;
    Backend backend;
    TopologyReader reader;
    int sequence = 0;
    DeviceProvisioningService service(
        authorizer,
        backend,
        std::chrono::minutes(5),
        [&] { return "free-plan-" + std::to_string(++sequence); },
        {},
        &reader
    );
    const auto topology = service.inspect_storage_topology(":1.50");
    const auto& free_region = std::get<UnallocatedRegion>(topology.devices.front().regions.back());
    const auto plan = service.build_device_preparation_plan(
        ":1.50",
        topology.generation,
        free_region.id,
        ProvisioningMode::CreatePartitionInUnallocatedSpace
    );
    const auto target = std::ranges::find(
        plan.after.regions,
        btrfsbackup::daemon::provisioning::PredictedRegionKind::BackupPartition,
        &btrfsbackup::daemon::provisioning::PredictedStorageRegion::kind
    );
    test_helpers::expect_true(
        "libfdisk geometry in plan",
        backend.geometry_plans == 1 && target != plan.after.regions.end() && target->geometry_exact &&
            target->start_sector == 5 && target->sector_count == 6 && target->partition_number == 2,
        "free-space plan did not use backend geometry"
    );
    const auto started = service.start(":1.50", 1000, plan_request(plan.id), 17);
    test_helpers::expect_true(
        "free-space execution target",
        started.operation_id == "prepare-1" && backend.starts == 1 &&
            backend.target.mode == ProvisioningMode::CreatePartitionInUnallocatedSpace &&
            backend.target.partition == std::nullopt && backend.target.free_region.has_value() &&
            backend.target.free_region->start_sector == 4 && backend.target.free_region->sector_count == 8 &&
            backend.target.planned_partition_geometry.has_value() &&
            backend.target.planned_partition_geometry->start_sector == 5 &&
            backend.target.planned_partition_geometry->sector_count == 6 &&
            backend.target.planned_partition_geometry->partition_number == 2,
        "free-space plan did not reach the backend with its frozen geometry"
    );
}
} // namespace

int main() {
    test_read_only_discovery_requires_only_an_active_caller();
    test_invalid_request_is_rejected_before_backend();
    test_topology_and_plan_are_caller_bound_and_revalidated();
    test_existing_target_inspection_is_caller_bound_and_invalidated_by_rescan();
    test_non_adoptable_inspection_has_no_reusable_token();
    test_free_space_plan_uses_backend_geometry();
    return test_helpers::finish("device provisioning service tests");
}
