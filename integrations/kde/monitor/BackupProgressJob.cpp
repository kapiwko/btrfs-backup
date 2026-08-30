// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupProgressJob.hpp"

#include <KLocalizedString>

#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace btrfsbackup::kde::monitor {

BackupProgressJob::BackupProgressJob(
    QString profile_id,
    QString run_id,
    QString profile_name,
    CancelRequest cancel_request,
    QObject* parent
)
    : KJob(parent),
      profile_id_(std::move(profile_id)),
      run_id_(std::move(run_id)),
      profile_name_(std::move(profile_name)),
      cancel_request_(std::move(cancel_request)) {
}

void BackupProgressJob::start() {
    if (started_) {
        return;
    }
    started_ = true;
    startElapsedTimer();
    QTimer::singleShot(0, this, [this]() {
        if (!finished_) {
            publish_description();
        }
    });
}

void BackupProgressJob::update(
    int progress,
    qint64 speed_bps,
    bool can_cancel,
    const QString& activity,
    const QString& source_name,
    const QString& target_name
) {
    if (finished_) {
        return;
    }

    can_cancel_ = can_cancel;
    update_capabilities();
    if (progress >= 0) {
        setPercent(static_cast<unsigned long>(std::clamp(progress, 0, 100)));
    }
    if (speed_bps >= 0) {
        const auto bounded_speed = static_cast<unsigned long>(std::min<quint64>(
            static_cast<quint64>(speed_bps),
            std::numeric_limits<unsigned long>::max()
        ));
        emitSpeed(bounded_speed);
    }

    const bool description_changed = source_name_ != source_name || target_name_ != target_name;
    source_name_ = source_name;
    target_name_ = target_name;
    if (started_ && description_changed) {
        publish_description();
    }
    if (!activity.isEmpty() && activity_ != activity) {
        activity_ = activity;
        Q_EMIT infoMessage(this, activity_);
    }
}

void BackupProgressJob::finish_successfully() {
    if (finished_) {
        return;
    }
    finished_ = true;
    setPercent(100);
    emitResult();
}

void BackupProgressJob::finish_with_error(const QString& message) {
    if (finished_) {
        return;
    }
    finished_ = true;
    setError(KJob::UserDefinedError);
    setErrorText(message);
    emitResult();
}

void BackupProgressJob::finish_cancelled() {
    if (finished_) {
        return;
    }
    finished_ = true;
    setError(KJob::KilledJobError);
    setErrorText(i18n("Backup cancelled"));
    emitResult();
}

void BackupProgressJob::stop_tracking() {
    if (finished_) {
        return;
    }
    finished_ = true;
    setCapabilities(KJob::NoCapabilities);
    deleteLater();
}

void BackupProgressJob::cancellation_rejected() {
    if (finished_ || !cancel_requested_) {
        return;
    }
    cancel_requested_ = false;
    update_capabilities();
}

QString BackupProgressJob::profile_id() const {
    return profile_id_;
}

QString BackupProgressJob::run_id() const {
    return run_id_;
}

bool BackupProgressJob::cancellation_requested() const {
    return cancel_requested_;
}

bool BackupProgressJob::doKill() {
    if (!can_cancel_ || cancel_requested_ || finished_) {
        return false;
    }
    cancel_requested_ = true;
    update_capabilities();
    cancel_request_(profile_id_, run_id_);
    return false;
}

void BackupProgressJob::update_capabilities() {
    setCapabilities(can_cancel_ && !cancel_requested_ ? KJob::Killable : KJob::NoCapabilities);
}

void BackupProgressJob::publish_description() {
    Q_EMIT description(
        this,
        i18n("Backing up %1", profile_name_),
        {i18n("Source"), source_name_},
        {i18n("Destination"), target_name_}
    );
}

} // namespace btrfsbackup::kde::monitor
