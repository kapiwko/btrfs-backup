// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace btrfsbackup {

enum class SnapshotSide {
    Local,
    Remote,
};

struct SnapshotName {
    std::string source_id;
    std::string timestamp;
    int sequence = 0;
    std::string name;
};

struct SnapshotMetadata {
    bool is_subvolume = false;
    bool readonly = false;
    std::string uuid;
    std::string received_uuid;
};

struct SnapshotInfo {
    SnapshotSide side = SnapshotSide::Local;
    std::string source_id;
    std::string name;
    std::string timestamp;
    int sequence = 0;
    std::filesystem::path path;
    bool readonly = false;
    std::string uuid;
    std::string received_uuid;
};

using SnapshotMetadataReader = std::function<std::optional<SnapshotMetadata>(const std::filesystem::path&)>;

std::optional<SnapshotName> parse_snapshot_name(const std::string& name, const std::string& source_id);

std::vector<SnapshotInfo> list_snapshot_inventory(
    const std::filesystem::path& directory,
    const std::string& source_id,
    SnapshotSide side,
    const SnapshotMetadataReader& metadata_reader
);

std::vector<SnapshotInfo> list_snapshot_inventory_at(
    const std::filesystem::path& scan_directory,
    const std::filesystem::path& reported_directory,
    const std::string& source_id,
    SnapshotSide side,
    const SnapshotMetadataReader& metadata_reader
);

} // namespace btrfsbackup
