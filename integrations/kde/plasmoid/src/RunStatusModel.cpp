// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RunStatusModel.hpp"

#include "ByteFormatting.hpp"
#include "ManagerApi.hpp"

#include <QDateTime>

#include <limits>

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

QString RunStatusModel::speedText() const {
    return btrfsbackup::kde::format_byte_rate(speed_bps_);
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

QString RunStatusModel::lastSuccessAt() const {
    return last_success_at_;
}

QString RunStatusModel::lastAttemptAt() const {
    return last_attempt_at_;
}

QString RunStatusModel::lastAttemptState() const {
    return last_attempt_state_;
}

QString RunStatusModel::freshnessState() const {
    return last_success_at_.isEmpty() ? QStringLiteral("unknown") : QStringLiteral("informational");
}

QString RunStatusModel::startedAt() const {
    return started_at_;
}

QString RunStatusModel::updatedAt() const {
    return updated_at_;
}

int RunStatusModel::elapsedSeconds() const {
    const QDateTime started = QDateTime::fromString(started_at_, Qt::ISODate);
    if (!started.isValid()) {
        return -1;
    }
    const QDateTime end = btrfsbackup::kde::active_run_state(state_)
        ? QDateTime::currentDateTimeUtc()
        : QDateTime::fromString(updated_at_, Qt::ISODate);
    if (!end.isValid()) {
        return -1;
    }
    return static_cast<int>(std::min<qint64>(
        std::max<qint64>(0, started.secsTo(end)),
        std::numeric_limits<int>::max()
    ));
}

int RunStatusModel::sourceIndex() const {
    return source_index_;
}

int RunStatusModel::sourceCount() const {
    return source_count_;
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
    last_success_at_ = status->last_success_at;
    last_attempt_at_ = status->last_attempt_at;
    last_attempt_state_ = status->last_attempt_state;
    started_at_ = status->started_at;
    updated_at_ = status->updated_at;
    source_index_ = status->source_index;
    source_count_ = status->source_count;
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
    last_success_at_.clear();
    last_attempt_at_.clear();
    last_attempt_state_.clear();
    started_at_.clear();
    updated_at_.clear();
    source_index_ = 0;
    source_count_ = 0;
    emit changed();
}
