// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/provisioning/ConfiguredBackupTargetMarker.hpp>

#include <algorithm>
#include <utility>

namespace btrfsbackup::daemon::provisioning {
namespace {

bool matches(const ExistingPartition& partition, const ConfiguredBackupTargetIdentity& target) {
    const bool partition_uuid_matches = !target.partition_uuid.empty() && partition.partition_uuid.has_value() &&
        *partition.partition_uuid == target.partition_uuid;
    const bool luks_uuid_matches = !target.luks_uuid.empty() && partition.filesystem.type == "crypto_LUKS" &&
        partition.filesystem.uuid == target.luks_uuid;
    return partition_uuid_matches || luks_uuid_matches;
}

void add_configured_target_blocker(ExistingPartition& partition) {
    const SafetyBlocker blocker{"configured-backup-target", partition.identity.display_path};
    if (std::ranges::find(partition.blockers, blocker) == partition.blockers.end())
        partition.blockers.push_back(blocker);
    partition.configured_backup_target = true;
    partition.suitable_for_reformat = false;
    partition.suitable_for_adoption = false;
}

} // namespace

ConfiguredBackupTargetMarker::ConfiguredBackupTargetMarker(
    std::vector<ConfiguredBackupTargetIdentity> targets
) : targets_(std::move(targets)) {
}

void ConfiguredBackupTargetMarker::apply(StorageTopology& topology) const {
    for (auto& device : topology.devices) {
        for (auto& region : device.regions) {
            auto* partition = std::get_if<ExistingPartition>(&region);
            if (partition != nullptr && std::ranges::any_of(targets_, [&](const auto& target) {
                    return matches(*partition, target);
                }))
                add_configured_target_blocker(*partition);
        }
    }
}

} // namespace btrfsbackup::daemon::provisioning
