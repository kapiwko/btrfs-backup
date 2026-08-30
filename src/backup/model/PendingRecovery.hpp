// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <backup/model/Snapshot.hpp>
#include <core/Identifiers.hpp>

namespace btrfsbackup::backup {

struct ClearPendingMarker {
    std::filesystem::path marker_path;
};

struct DeletePendingLocalSnapshot {
    std::filesystem::path snapshot_path;
};

struct DeletePendingRemoteSnapshot {
    std::filesystem::path snapshot_path;
};

using PendingRecoveryEffect = std::variant<
    DeletePendingRemoteSnapshot,
    DeletePendingLocalSnapshot,
    ClearPendingMarker>;

struct PendingMarker {
    std::string source_name;
    std::string local_snapshot_path;
    std::string final_snapshot_path;
    std::string run_id;
    std::string timestamp;
};

struct PendingRecoveryPlan {
    std::filesystem::path marker_path;
    std::filesystem::path pending_snapshot_path;
    std::vector<PendingRecoveryEffect> effects;
    std::string message;

    [[nodiscard]] bool required() const noexcept {
        return !effects.empty();
    }
};

template <typename Effect>
[[nodiscard]] const Effect* pending_recovery_effect(const PendingRecoveryPlan& plan) noexcept {
    for (const PendingRecoveryEffect& effect : plan.effects) {
        if (const auto* typed = std::get_if<Effect>(&effect)) {
            return typed;
        }
    }
    return nullptr;
}

PendingRecoveryPlan plan_pending_recovery(
    const SourceId& source_id,
    const std::filesystem::path& profile_state_dir,
    const std::filesystem::path& local_snapshot_dir,
    const std::filesystem::path& remote_snapshot_dir,
    const std::optional<PendingMarker>& marker,
    const std::optional<SnapshotMetadata>& pending_snapshot,
    const std::vector<SnapshotInfo>& remote_snapshots,
    bool keep_failed_local_snapshot
);

} // namespace btrfsbackup::backup
