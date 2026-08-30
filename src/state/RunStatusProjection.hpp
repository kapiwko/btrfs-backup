// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <backup/model/BackupRunEvent.hpp>
#include <backup/ports/IBackupRunEventSink.hpp>
#include <state/PersistentDocumentOperations.hpp>
#include <state/RunStatusBuilder.hpp>

namespace btrfsbackup::state {

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
    RunStatusBuilder builder_;
    std::optional<RunId> run_id_;
    btrfsbackup::backup::OperationKind operation_kind_ = btrfsbackup::backup::OperationKind::Backup;
    std::optional<PendingActionFailure> pending_action_failure_;
    bool run_started_ = false;
    int last_overall_progress_ = -1;
};

} // namespace btrfsbackup::state
