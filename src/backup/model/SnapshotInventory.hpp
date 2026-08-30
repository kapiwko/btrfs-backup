// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <core/Identifiers.hpp>
#include <core/RuntimeTime.hpp>

namespace btrfsbackup::backup {

enum class SnapshotSide {
    Local,
    Remote,
};

class SnapshotUuid {
  public:
    SnapshotUuid() = default;
    explicit SnapshotUuid(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    auto operator<=>(const SnapshotUuid&) const = default;

  private:
    std::string value_;
};

class ReceivedSnapshotUuid {
  public:
    ReceivedSnapshotUuid() = default;
    explicit ReceivedSnapshotUuid(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    auto operator<=>(const ReceivedSnapshotUuid&) const = default;

  private:
    std::string value_;
};

[[nodiscard]] bool uuid_matches(const SnapshotUuid& snapshot, const ReceivedSnapshotUuid& received) noexcept;

struct SnapshotName {
    SourceId source_id;
    RuntimeTimePoint timestamp;
    int sequence = 0;
    std::string name;
};

struct SnapshotMetadata {
    bool is_subvolume = false;
    bool readonly = false;
    SnapshotUuid uuid;
    ReceivedSnapshotUuid received_uuid;
};

struct SnapshotInfo {
    SnapshotSide side = SnapshotSide::Local;
    SourceId source_id;
    std::string name;
    RuntimeTimePoint timestamp;
    int sequence = 0;
    std::filesystem::path path;
    bool readonly = false;
    SnapshotUuid uuid;
    ReceivedSnapshotUuid received_uuid;
};

using SnapshotMetadataReader = std::function<std::optional<SnapshotMetadata>(const std::filesystem::path&)>;

std::optional<SnapshotName> parse_snapshot_name(const std::string& name, const SourceId& source_id);

std::vector<SnapshotInfo> list_snapshot_inventory(
    const std::filesystem::path& directory,
    const SourceId& source_id,
    SnapshotSide side,
    const SnapshotMetadataReader& metadata_reader
);

std::vector<SnapshotInfo> list_snapshot_inventory_at(
    const std::filesystem::path& scan_directory,
    const std::filesystem::path& reported_directory,
    const SourceId& source_id,
    SnapshotSide side,
    const SnapshotMetadataReader& metadata_reader
);

} // namespace btrfsbackup::backup
