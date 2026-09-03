// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <provisioning/StorageTopology.hpp>

namespace btrfsbackup::provisioning {

struct ConfiguredBackupTargetIdentity {
    std::string partition_uuid;
    std::string luks_uuid;
};

class ConfiguredBackupTargetMarker final {
  public:
    explicit ConfiguredBackupTargetMarker(std::vector<ConfiguredBackupTargetIdentity> targets);
    void apply(StorageTopology& topology) const;

  private:
    std::vector<ConfiguredBackupTargetIdentity> targets_;
};

} // namespace btrfsbackup::provisioning
