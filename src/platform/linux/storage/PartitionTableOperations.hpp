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

class IPartitionTableOperations {
  public:
    virtual ~IPartitionTableOperations() = default;
    [[nodiscard]] virtual PlannedPartitionGeometry plan_partition_in_free_space(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size,
        std::uint64_t free_start_sector,
        std::uint64_t free_sector_count
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
    virtual void replace_with_single_gpt_partition(
        const std::filesystem::path& device,
        const std::string& expected_major_minor
    ) = 0;
};

class LibfdiskPartitionTableOperations final : public IPartitionTableOperations {
  public:
    [[nodiscard]] PlannedPartitionGeometry plan_partition_in_free_space(
        const std::filesystem::path& device,
        const std::string& expected_major_minor,
        const std::string& expected_partition_table_id,
        std::uint32_t expected_logical_sector_size,
        std::uint64_t free_start_sector,
        std::uint64_t free_sector_count
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
    void replace_with_single_gpt_partition(
        const std::filesystem::path& device,
        const std::string& expected_major_minor
    ) override;
};

} // namespace btrfsbackup::platform::linux::storage
