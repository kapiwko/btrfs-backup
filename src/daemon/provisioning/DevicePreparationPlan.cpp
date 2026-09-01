// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/provisioning/DevicePreparationPlan.hpp>

namespace btrfsbackup::daemon::provisioning {

std::string provisioning_mode_name(ProvisioningMode mode) {
    switch (mode) {
    case ProvisioningMode::EraseWholeDevice:
        return "erase-whole-device";
    case ProvisioningMode::ReformatExistingPartition:
        return "reformat-existing-partition";
    case ProvisioningMode::CreatePartitionInUnallocatedSpace:
        return "create-partition-in-unallocated-space";
    case ProvisioningMode::AdoptExistingTarget:
        return "adopt-existing-target";
    }
    return "unsupported";
}

std::string predicted_region_kind_name(PredictedRegionKind kind) {
    switch (kind) {
    case PredictedRegionKind::ExistingPartition:
        return "existing-partition";
    case PredictedRegionKind::Unallocated:
        return "unallocated";
    case PredictedRegionKind::BackupPartition:
        return "backup-partition";
    }
    return "unsupported";
}

} // namespace btrfsbackup::daemon::provisioning
