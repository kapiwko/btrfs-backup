// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <backup/model/PendingRecovery.hpp>
#include <config/ConfigurationIdentity.hpp>
#include <config/model/TargetIdentity.hpp>
#include <core/Identifiers.hpp>
#include <core/RuntimeTime.hpp>
#include <state/PersistentDocumentOperations.hpp>

namespace btrfsbackup::state {

struct SuccessState {
    LocalDate date;
    RuntimeTimePoint timestamp;
    RunId run_id;
    ProfileId profile_id;
    std::string profile_name;
    int source_count = 0;
    btrfsbackup::config::LuksUuid target_luks_uuid;
    btrfsbackup::config::ConfigurationFingerprint config_fingerprint;
};

bool last_success_matches(
    const std::filesystem::path& profile_state_dir,
    LocalDate today,
    const btrfsbackup::config::LuksUuid& target_luks_uuid,
    const btrfsbackup::config::ConfigurationFingerprint& config_fingerprint
);

void write_success_state(
    IAtomicDocumentWriter& files,
    const std::filesystem::path& profile_state_dir,
    const SuccessState& state
);
std::filesystem::path cancel_request_path(const std::filesystem::path& profile_state_dir);
std::filesystem::path active_run_path(const std::filesystem::path& profile_state_dir);
void write_active_run(
    IAtomicDocumentWriter& files,
    const std::filesystem::path& profile_state_dir,
    const RunId& run_id
);
[[nodiscard]] std::optional<RunId> active_run(const std::filesystem::path& profile_state_dir);
void clear_active_run(
    IDurableDocumentRemover& files,
    const std::filesystem::path& profile_state_dir,
    const RunId& run_id
);
void write_cancel_request(
    IAtomicDocumentWriter& files,
    const std::filesystem::path& profile_state_dir,
    const RunId& run_id
);
bool cancel_requested(const std::filesystem::path& profile_state_dir);
bool cancel_requested(const std::filesystem::path& profile_state_dir, const RunId& run_id);
void clear_cancel_request(IDurableDocumentRemover& files, const std::filesystem::path& profile_state_dir);
void clear_cancel_request(
    IDurableDocumentRemover& files,
    const std::filesystem::path& profile_state_dir,
    const RunId& run_id
);
std::filesystem::path pending_marker_path(const std::filesystem::path& profile_state_dir, const SourceId& source_id);
void write_pending_marker(
    IAtomicDocumentWriter& files,
    const std::filesystem::path& profile_state_dir,
    const btrfsbackup::backup::PendingMarker& marker
);
std::string read_pending_marker_field(const std::filesystem::path& marker_path, const std::string& field);
void clear_pending_marker(IDurableDocumentRemover& files, const std::filesystem::path& marker_path);

} // namespace btrfsbackup::state
