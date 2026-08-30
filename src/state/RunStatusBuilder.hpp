// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <backup/model/BackupRunEvent.hpp>
#include <core/RuntimeTime.hpp>
#include <state/RunStatus.hpp>

namespace btrfsbackup::state {

struct BackupRunStatusContext {
    std::filesystem::path status_root;
    std::filesystem::path history_root;
    std::string profile_name;
    int source_count = 0;
    RuntimeTimePoint started_at;
    std::map<SourceId, std::string> source_names;
    std::string target_name;
};

struct RunEventData {
    btrfsbackup::backup::BackupRunEventKind kind;
    btrfsbackup::backup::OperationKind operation_kind;
    ProfileId profile_id;
    RunId run_id;
    std::optional<SourceId> source_id;
    int source_index = 0;
    std::optional<btrfsbackup::backup::BackupRunActionKind> action_kind;
    btrfsbackup::backup::BackupTransferStage transfer_stage = btrfsbackup::backup::BackupTransferStage::Transferring;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t bytes_produced = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t run_bytes_transferred = 0;
    std::uint64_t delta_bytes = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t speed_bps = 0;
    std::optional<ErrorCode> error_code;
    std::string message;
};

class RunStatusBuilder {
  public:
    explicit RunStatusBuilder(const BackupRunStatusContext& context);

    [[nodiscard]] RunEventData read_event(
        const btrfsbackup::backup::BackupRunEvent& event
    ) const;
    [[nodiscard]] RunStatus build(
        const RunEventData& event,
        int minimum_overall_progress
    ) const;

  private:
    const BackupRunStatusContext& context_;
};

} // namespace btrfsbackup::state
