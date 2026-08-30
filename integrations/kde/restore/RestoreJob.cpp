// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreJob.hpp"

#include <KLocalizedString>

#include <QMetaObject>

#include <platform/linux/restore/PosixRestoreOperations.hpp>
#include <restore/RestoreEngine.hpp>

namespace btrfsbackup::kde::restore {

RestoreJob::RestoreJob(btrfsbackup::restore::RestorePlan plan, QObject* parent)
    : KJob(parent), plan_(std::move(plan)) {
    setCapabilities(KJob::Killable);
}

RestoreJob::~RestoreJob() {
    cancellation_.request_cancel();
}

void RestoreJob::start() {
    if (started_)
        return;
    started_ = true;
    Q_EMIT description(this, i18n("Restoring backup"),
        {i18n("Source"), QString::fromStdString(plan_.source.filename().string())},
        {i18n("Destination"), QString::fromStdString(plan_.destination.string())});
    worker_ = std::jthread([this] {
        try {
            btrfsbackup::platform::linux::restore::PosixRestoreOperations operations;
            btrfsbackup::restore::RestoreExecutor executor(operations);
            const auto result = executor.execute(plan_, cancellation_);
            QMetaObject::invokeMethod(this, [this, result] {
                setProcessedAmount(KJob::Files, result.statistics.files);
                setProcessedAmount(KJob::Bytes, result.statistics.bytes);
                finish(true, {});
            }, Qt::QueuedConnection);
        } catch (const std::exception& error) {
            const QString message = QString::fromUtf8(error.what());
            QMetaObject::invokeMethod(this, [this, message] { finish(false, message); }, Qt::QueuedConnection);
        }
    });
}

bool RestoreJob::doKill() {
    cancellation_.request_cancel();
    setCapabilities(KJob::NoCapabilities);
    return false;
}

void RestoreJob::finish(bool success, QString error) {
    setCapabilities(KJob::NoCapabilities);
    if (!success) {
        setError(cancellation_.cancellation_requested() ? KJob::KilledJobError : KJob::UserDefinedError);
        setErrorText(std::move(error));
    }
    emitResult();
}

} // namespace btrfsbackup::kde::restore
