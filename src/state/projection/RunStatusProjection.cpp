// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/projection/RunStatusProjection.hpp>

#include <utility>

#include <state/query/RunHistory.hpp>
#include <state/persistence/StatusWriter.hpp>

namespace btrfsbackup::state {

namespace {

bool should_write_status(btrfsbackup::backup::BackupRunEventKind kind) {
    return kind == btrfsbackup::backup::BackupRunEventKind::RunStarted || kind == btrfsbackup::backup::BackupRunEventKind::SourceStarted || kind == btrfsbackup::backup::BackupRunEventKind::ActionStarted || kind == btrfsbackup::backup::BackupRunEventKind::TransferProgress || kind == btrfsbackup::backup::BackupRunEventKind::ActionCompleted || kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed || kind == btrfsbackup::backup::BackupRunEventKind::SourceCompleted || kind == btrfsbackup::backup::BackupRunEventKind::TargetValidationCompleted || kind == btrfsbackup::backup::BackupRunEventKind::RunCompleted || kind == btrfsbackup::backup::BackupRunEventKind::RunFailed || kind == btrfsbackup::backup::BackupRunEventKind::RunCancelled;
}

bool should_write_history(
    btrfsbackup::backup::BackupRunEventKind kind,
    btrfsbackup::backup::OperationKind operation_kind
) {
    return operation_kind == btrfsbackup::backup::OperationKind::Backup &&
        (kind == btrfsbackup::backup::BackupRunEventKind::RunFailed ||
         kind == btrfsbackup::backup::BackupRunEventKind::RunCompleted ||
         kind == btrfsbackup::backup::BackupRunEventKind::RunCancelled);
}

bool is_terminal_event(btrfsbackup::backup::BackupRunEventKind kind) {
    return kind == btrfsbackup::backup::BackupRunEventKind::TargetValidationCompleted ||
        kind == btrfsbackup::backup::BackupRunEventKind::RunCompleted ||
        kind == btrfsbackup::backup::BackupRunEventKind::RunFailed ||
        kind == btrfsbackup::backup::BackupRunEventKind::RunCancelled;
}

} // namespace

RunStatusProjection::RunStatusProjection(IAtomicDocumentWriter& files, BackupRunStatusContext context)
    : files_(files), context_(std::move(context)), builder_(context_) {
}

void RunStatusProjection::on_backup_run_event(const btrfsbackup::backup::BackupRunEvent& event) {
    RunEventData data = builder_.read_event(event);
    if (!should_write_status(data.kind)) {
        return;
    }

    if (data.run_id != run_id_) {
        run_id_ = data.run_id;
        pending_action_failure_.reset();
        run_started_ = data.kind == btrfsbackup::backup::BackupRunEventKind::RunStarted;
        last_overall_progress_ = -1;
        operation_kind_ = data.operation_kind;
    } else if (data.kind == btrfsbackup::backup::BackupRunEventKind::RunStarted) {
        run_started_ = true;
        pending_action_failure_.reset();
        last_overall_progress_ = -1;
        operation_kind_ = data.operation_kind;
    }
    if (data.kind == btrfsbackup::backup::BackupRunEventKind::TargetValidationCompleted) {
        operation_kind_ = btrfsbackup::backup::OperationKind::TargetValidation;
    }
    data.operation_kind = operation_kind_;
    if (data.kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed &&
        data.source_id.has_value() && data.action_kind.has_value()) {
        pending_action_failure_ = PendingActionFailure{
            .run_id = data.run_id,
            .source_id = *data.source_id,
            .source_index = data.source_index,
            .action_kind = *data.action_kind,
        };
    } else if (data.kind == btrfsbackup::backup::BackupRunEventKind::RunFailed && pending_action_failure_.has_value() && pending_action_failure_->run_id == data.run_id) {
        data.source_id = pending_action_failure_->source_id;
        data.source_index = pending_action_failure_->source_index;
        data.action_kind = pending_action_failure_->action_kind;
    }

    RunStatus status = builder_.build(data, last_overall_progress_);
    if (status.progress.overall_percent.has_value()) {
        last_overall_progress_ = *status.progress.overall_percent;
    }
    if (data.kind != btrfsbackup::backup::BackupRunEventKind::RunFailed || run_started_) {
        write_current_status(files_, context_.status_root, status);
    }
    if (should_write_history(data.kind, data.operation_kind)) {
        write_history_entry(files_, context_.history_root, status);
    }
    if (is_terminal_event(data.kind)) {
        pending_action_failure_.reset();
        run_started_ = false;
    }
}

} // namespace btrfsbackup::state
