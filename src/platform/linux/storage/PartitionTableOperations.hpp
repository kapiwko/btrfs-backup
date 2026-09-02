// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace btrfsbackup::platform::linux::storage {

struct PlannedPartitionGeometry {
    std::uint64_t start_sector = 0;
    std::uint64_t sector_count = 0;
    std::uint32_t partition_number = 0;
    bool operator==(const PlannedPartitionGeometry&) const = default;
};

enum class PartitionCreationState {
    NotCreated,
    Created,
    Conflict,
};

struct PartitionCreationInspection {
    PartitionCreationState state = PartitionCreationState::Conflict;
    std::filesystem::path partition;
};

enum class PartitionTableFormat {
    None,
    Gpt,
    Mbr,
};

class IPartitionTableOperations {
  public:
    virtual ~IPartitionTableOperations() = default;
    [[nodiscard]] virtual std::string snapshot_partition_table(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        PartitionTableFormat expected_format,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size
    ) const = 0;
    [[nodiscard]] virtual PlannedPartitionGeometry plan_partition_in_free_space(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size,
        std::uint64_t free_start_sector,
        std::uint64_t free_sector_count
    ) const = 0;
    [[nodiscard]] virtual PlannedPartitionGeometry plan_single_gpt_partition(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        std::uint32_t expected_logical_sector_size
    ) const = 0;
    [[nodiscard]] virtual PartitionCreationInspection inspect_partition_creation(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size,
        std::uint64_t original_free_start_sector,
        std::uint64_t original_free_sector_count,
        const PlannedPartitionGeometry& geometry
    ) const = 0;
    [[nodiscard]] virtual std::filesystem::path create_partition_in_free_space(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size,
        std::uint64_t free_start_sector,
        std::uint64_t free_sector_count,
        const PlannedPartitionGeometry& geometry
    ) = 0;
    [[nodiscard]] virtual std::filesystem::path replace_with_single_gpt_partition(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const PlannedPartitionGeometry& geometry
    ) = 0;
};

class LibfdiskPartitionTableOperations final : public IPartitionTableOperations {
  public:
    [[nodiscard]] std::string snapshot_partition_table(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        PartitionTableFormat expected_format,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size
    ) const override;
    [[nodiscard]] PlannedPartitionGeometry plan_partition_in_free_space(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size,
        std::uint64_t free_start_sector,
        std::uint64_t free_sector_count
    ) const override;
    [[nodiscard]] PlannedPartitionGeometry plan_single_gpt_partition(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        std::uint32_t expected_logical_sector_size
    ) const override;
    [[nodiscard]] PartitionCreationInspection inspect_partition_creation(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size,
        std::uint64_t original_free_start_sector,
        std::uint64_t original_free_sector_count,
        const PlannedPartitionGeometry& geometry
    ) const override;
    [[nodiscard]] std::filesystem::path create_partition_in_free_space(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size,
        std::uint64_t free_start_sector,
        std::uint64_t free_sector_count,
        const PlannedPartitionGeometry& geometry
    ) override;
    [[nodiscard]] std::filesystem::path replace_with_single_gpt_partition(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const PlannedPartitionGeometry& geometry
    ) override;
};

} // namespace btrfsbackup::platform::linux::storage
