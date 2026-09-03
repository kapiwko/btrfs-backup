// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <provisioning/ConfiguredBackupTargetMarker.hpp>
#include <provisioning/StorageTopologyReader.hpp>

namespace btrfsbackup::platform::linux::storage::provisioning {

struct SystemStorageTopologyPaths {
    std::filesystem::path sysfs_root = "/sys";
    std::filesystem::path mountinfo = "/proc/self/mountinfo";
    std::filesystem::path swaps = "/proc/swaps";
};

using ConfiguredBackupTargetProvider =
    std::function<std::vector<::btrfsbackup::provisioning::ConfiguredBackupTargetIdentity>()>;

class SystemStorageTopologyReader final : public ::btrfsbackup::provisioning::StorageTopologyReader {
  public:
    explicit SystemStorageTopologyReader(
        SystemStorageTopologyPaths paths = {},
        ConfiguredBackupTargetProvider configured_targets = {}
    );

    [[nodiscard]] ::btrfsbackup::provisioning::StorageTopology scan() override;

  private:
    SystemStorageTopologyPaths paths_;
    ConfiguredBackupTargetProvider configured_targets_;
};

} // namespace btrfsbackup::platform::linux::storage::provisioning
