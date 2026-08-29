// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <backup/model/backup_run_event.hpp>
#include <backup/ports/backup_run_event_sink.hpp>
#include <core/runtime_time.hpp>
#include <state/persistent_document_operations.hpp>
#include <state/status_writer.hpp>

namespace btrfsbackup::state {

struct BackupRunStatusContext {
    std::filesystem::path status_root;
    std::filesystem::path history_root;
    std::string profile_name;
    int source_count = 0;
    RuntimeTimePoint started_at;
    std::map<std::string, std::string> source_names;
    std::string target_name;
};

class RunStatusProjection final : public btrfsbackup::backup::IBackupRunEventSink {
  public:
    RunStatusProjection(IAtomicDocumentWriter& files, BackupRunStatusContext context);

    void on_backup_run_event(const btrfsbackup::backup::BackupRunEvent& event) override;

  private:
    struct PendingActionFailure {
        RunId run_id;
        SourceId source_id;
        int source_index = 0;
        btrfsbackup::backup::BackupRunActionKind action_kind;
    };

    IAtomicDocumentWriter& files_;
    BackupRunStatusContext context_;
    std::optional<RunId> run_id_;
    std::optional<PendingActionFailure> pending_action_failure_;
    bool run_started_ = false;
    int last_overall_progress_ = -1;
};

} // namespace btrfsbackup::state
