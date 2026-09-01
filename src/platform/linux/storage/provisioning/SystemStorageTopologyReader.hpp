// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <daemon/provisioning/StorageTopologyReader.hpp>

namespace btrfsbackup::platform::linux::storage {

struct SystemStorageTopologyPaths {
    std::filesystem::path sysfs_root = "/sys";
    std::filesystem::path mountinfo = "/proc/self/mountinfo";
    std::filesystem::path swaps = "/proc/swaps";
};

class SystemStorageTopologyReader final : public daemon::provisioning::StorageTopologyReader {
  public:
    explicit SystemStorageTopologyReader(SystemStorageTopologyPaths paths = {});

    [[nodiscard]] daemon::provisioning::StorageTopology scan() override;

  private:
    SystemStorageTopologyPaths paths_;
};

} // namespace btrfsbackup::platform::linux::storage
