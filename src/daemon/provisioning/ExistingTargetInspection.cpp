// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/provisioning/ExistingTargetInspection.hpp>

namespace btrfsbackup::daemon::provisioning {

std::string existing_target_classification_name(ExistingTargetClassification classification) {
    switch (classification) {
    case ExistingTargetClassification::CompatibleRepository:
        return "compatible-repository";
    case ExistingTargetClassification::EmptyFilesystem:
        return "empty-filesystem";
    case ExistingTargetClassification::LegacyRepository:
        return "legacy-repository";
    case ExistingTargetClassification::UnsupportedRepository:
        return "unsupported-repository";
    case ExistingTargetClassification::ForeignOrInvalidRepository:
        return "foreign-or-invalid-repository";
    case ExistingTargetClassification::NotBtrfsFilesystem:
        return "not-btrfs-filesystem";
    }
    return "foreign-or-invalid-repository";
}

} // namespace btrfsbackup::daemon::provisioning
