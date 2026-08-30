// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RunStatusModel.hpp"

#include "ManagerApi.hpp"

RunStatusModel::RunStatusModel(QObject* parent)
    : QObject(parent) {
}

QString RunStatusModel::state() const {
    return state_;
}

QString RunStatusModel::runId() const {
    return run_id_;
}

QString RunStatusModel::phase() const {
    return phase_;
}

QString RunStatusModel::activity() const {
    return activity_;
}

bool RunStatusModel::canCancel() const {
    return cancel_supported_ && can_cancel_ && !run_id_.isEmpty();
}

QString RunStatusModel::sourceName() const {
    return source_name_;
}

QString RunStatusModel::targetName() const {
    return target_name_;
}

qint64 RunStatusModel::speedBps() const {
    return speed_bps_;
}

int RunStatusModel::etaSeconds() const {
    return eta_seconds_;
}

int RunStatusModel::sourceProgress() const {
    return source_progress_;
}

int RunStatusModel::overallProgress() const {
    return overall_progress_;
}

QString RunStatusModel::progressAccuracy() const {
    return progress_accuracy_;
}

QString RunStatusModel::errorCode() const {
    return error_code_;
}

void RunStatusModel::setCancelSupported(bool supported) {
    if (cancel_supported_ == supported) {
        return;
    }
    cancel_supported_ = supported;
    emit changed();
}

bool RunStatusModel::apply(const QString& payload) {
    const auto status = btrfsbackup::kde::parse_status(payload);
    if (!status.has_value()) {
        return false;
    }

    const bool was_active = btrfsbackup::kde::active_run_state(state_);
    run_id_ = status->run_id;
    state_ = status->state;
    phase_ = status->phase;
    activity_ = status->activity;
    can_cancel_ = status->can_cancel;
    source_name_ = status->source_name;
    target_name_ = status->target_name;
    speed_bps_ = status->speed_bps;
    eta_seconds_ = static_cast<int>(status->eta_seconds);
    source_progress_ = status->source_progress;
    overall_progress_ = status->overall_progress;
    progress_accuracy_ = status->progress_accuracy;
    error_code_ = status->error_code;
    emit changed();
    if (was_active && !btrfsbackup::kde::active_run_state(state_)) {
        emit activeRunFinished();
    }
    return true;
}

void RunStatusModel::reset() {
    run_id_.clear();
    state_ = QStringLiteral("unknown");
    phase_ = QStringLiteral("idle");
    activity_ = QStringLiteral("idle");
    can_cancel_ = false;
    source_name_.clear();
    target_name_.clear();
    speed_bps_ = 0;
    eta_seconds_ = -1;
    source_progress_ = -1;
    overall_progress_ = -1;
    progress_accuracy_ = QStringLiteral("indeterminate");
    error_code_.clear();
    emit changed();
}
