// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <daemon/control/OperationalControlService.hpp>
#include <daemon/control/SourceCandidate.hpp>
#include <provisioning/DevicePreparation.hpp>
#include <provisioning/DevicePreparationPlanBuilder.hpp>
#include <provisioning/ExistingTargetInspection.hpp>
#include <provisioning/StorageSafetyInspector.hpp>
#include <provisioning/StorageTopologyReader.hpp>

namespace btrfsbackup::daemon::control {

using provisioning::DevicePreparationOwner;
using provisioning::DevicePreparationRequest;
using provisioning::DevicePreparationStatus;
using provisioning::DevicePreparationTarget;
using provisioning::ProvisioningDevice;

class IDeviceProvisioningBackend {
  public:
    virtual ~IDeviceProvisioningBackend() = default;
    [[nodiscard]] virtual std::vector<SourceCandidate> list_source_candidates() = 0;
    [[nodiscard]] virtual std::vector<std::string> inspect_safety(
        const DevicePreparationTarget& target
    ) const = 0;
    [[nodiscard]] virtual provisioning::PlannedPartitionGeometry plan_partition_geometry(
        const provisioning::StorageDevice& device,
        const provisioning::UnallocatedRegion& free_region
    ) const = 0;
    [[nodiscard]] virtual provisioning::PlannedPartitionGeometry plan_whole_device_partition_geometry(
        const provisioning::StorageDevice& device
    ) const = 0;
    [[nodiscard]] virtual provisioning::ExistingTargetInspectionSummary inspect_existing_target(
        const DevicePreparationTarget& target,
        int credential_fd
    ) = 0;
    [[nodiscard]] virtual DevicePreparationStatus start(
        const DevicePreparationRequest& request,
        const DevicePreparationTarget& target,
        const DevicePreparationOwner& owner,
        int passphrase_fd
    ) = 0;
    [[nodiscard]] virtual DevicePreparationStatus status(const std::string& operation_id) const = 0;
    [[nodiscard]] virtual bool owned_by(
        const std::string& operation_id,
        const DevicePreparationOwner& owner
    ) const = 0;
    virtual void cancel(const std::string& operation_id) = 0;
};

using ProvisioningCandidateClock = std::function<std::chrono::steady_clock::time_point()>;
using ProvisioningCandidateIdGenerator = std::function<std::string()>;

class DeviceProvisioningService final {
  public:
    DeviceProvisioningService(
        IManagerAuthorizer& authorizer,
        IDeviceProvisioningBackend& backend,
        std::chrono::seconds candidate_lifetime = std::chrono::minutes(5),
        ProvisioningCandidateIdGenerator candidate_ids = {},
        ProvisioningCandidateClock clock = {},
        provisioning::StorageTopologyReader* topology_reader = nullptr
    );
    [[nodiscard]] provisioning::StorageTopology inspect_storage_topology(const std::string& caller);
    [[nodiscard]] provisioning::ExistingTargetInspection inspect_existing_target(
        const std::string& caller,
        const provisioning::TopologyGeneration& expected_generation,
        const provisioning::PartitionCandidateId& partition_id,
        int credential_fd
    );
    [[nodiscard]] provisioning::DevicePreparationPlan build_device_preparation_plan(
        const std::string& caller,
        const provisioning::TopologyGeneration& expected_generation,
        const std::string& selected_candidate_id,
        provisioning::ProvisioningMode mode,
        const std::string& inspection_id = {}
    );
    [[nodiscard]] std::vector<SourceCandidate> list_source_candidates(const std::string& caller);
    [[nodiscard]] DevicePreparationStatus start(
        const std::string& caller,
        std::uint32_t caller_uid,
        const DevicePreparationRequest& request,
        int passphrase_fd
    );
    [[nodiscard]] DevicePreparationStatus status(
        const std::string& caller,
        std::uint32_t caller_uid,
        const std::string& operation_id
    ) const;
    void cancel(const std::string& caller, std::uint32_t caller_uid, const std::string& operation_id);

  private:
    struct TopologySnapshot {
        provisioning::StorageTopology topology;
        std::string caller;
        std::chrono::steady_clock::time_point expires_at;
    };
    struct StoredPlan {
        provisioning::DevicePreparationPlan plan;
        std::string caller;
        std::chrono::steady_clock::time_point expires_at;
    };
    struct StoredInspection {
        provisioning::ExistingTargetInspection inspection;
        std::string caller;
        std::chrono::steady_clock::time_point expires_at;
    };
    struct StoredSourceCandidate {
        SourceCandidate candidate;
        std::string caller;
        std::chrono::steady_clock::time_point expires_at;
    };
    [[nodiscard]] provisioning::DevicePreparationPlan find_plan(
        const std::string& caller,
        const std::string& plan_id
    );
    [[nodiscard]] provisioning::DevicePreparationPlan take_plan(
        const std::string& caller,
        const std::string& plan_id
    );
    [[nodiscard]] provisioning::ExistingTargetInspection find_inspection(
        const std::string& caller,
        const std::string& inspection_id
    );
    void consume_inspection(const std::string& caller, const std::string& inspection_id);
    [[nodiscard]] provisioning::StorageTopology find_topology(
        const std::string& caller,
        const provisioning::TopologyGeneration& generation
    );
    [[nodiscard]] SourceCandidate find_source_candidate(
        const std::string& caller,
        const std::string& candidate_id
    );
    void expire_candidates(std::chrono::steady_clock::time_point now);
    void require_active_caller(const std::string& caller) const;
    void authorize(const std::string& caller, std::string_view method) const;
    void authorize_owner_or_admin(
        const std::string& caller,
        std::uint32_t caller_uid,
        const std::string& operation_id,
        std::string_view method
    ) const;
    IManagerAuthorizer& authorizer_;
    IDeviceProvisioningBackend& backend_;
    std::chrono::seconds candidate_lifetime_;
    ProvisioningCandidateIdGenerator candidate_ids_;
    ProvisioningCandidateClock clock_;
    std::mutex candidates_mutex_;
    std::map<std::string, TopologySnapshot> topologies_;
    std::map<std::string, StoredPlan> plans_;
    std::map<std::string, StoredInspection> inspections_;
    std::map<std::string, StoredSourceCandidate> source_candidates_;
    provisioning::StorageTopologyReader* topology_reader_;
    provisioning::DevicePreparationPlanBuilder plan_builder_;
    provisioning::StorageSafetyInspector storage_safety_inspector_;
};

} // namespace btrfsbackup::daemon::control
