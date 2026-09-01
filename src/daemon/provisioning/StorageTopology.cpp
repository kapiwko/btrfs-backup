// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/provisioning/StorageTopology.hpp>

namespace btrfsbackup::daemon::provisioning {

std::string partition_table_type_name(PartitionTableType type) {
    switch (type) {
    case PartitionTableType::None:
        return "none";
    case PartitionTableType::Gpt:
        return "gpt";
    case PartitionTableType::Mbr:
        return "mbr";
    case PartitionTableType::Unsupported:
        return "unsupported";
    }
    return "unsupported";
}

std::uint64_t region_start_sector(const StorageRegion& region) {
    return std::visit([](const auto& value) { return value.start_sector; }, region);
}

} // namespace btrfsbackup::daemon::provisioning
