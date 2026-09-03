// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <provisioning/StorageTopology.hpp>

namespace btrfsbackup::provisioning {

enum class ExistingTargetClassification {
    CompatibleRepository,
    EmptyFilesystem,
    LegacyRepository,
    UnsupportedRepository,
    ForeignOrInvalidRepository,
    NotBtrfsFilesystem,
};

[[nodiscard]] std::string existing_target_classification_name(ExistingTargetClassification classification);

struct ExistingTargetInspectionSummary {
    ExistingTargetClassification classification = ExistingTargetClassification::CompatibleRepository;
    std::string diagnostic_code;
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string partition_uuid;
    std::string repository_id;
    std::uint64_t catalog_generation = 0;
    std::size_t snapshot_count = 0;

    [[nodiscard]] bool adoptable() const noexcept {
        return classification == ExistingTargetClassification::CompatibleRepository;
    }

    bool operator==(const ExistingTargetInspectionSummary&) const = default;
};

struct ExistingTargetInspection {
    std::string inspection_id;
    TopologyGeneration topology_generation;
    DeviceCandidateId device_id;
    PartitionCandidateId partition_id;
    ExistingTargetInspectionSummary target;

    bool operator==(const ExistingTargetInspection&) const = default;
};

} // namespace btrfsbackup::provisioning
