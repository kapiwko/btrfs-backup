// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreJob.hpp"

#include "RestoreErrorPresentation.hpp"

#include <KLocalizedString>

#include <QMetaObject>

#include <platform/linux/restore/PosixRestoreOperations.hpp>
#include <restore/RestoreEngine.hpp>

namespace btrfsbackup::kde::restore {

RestoreJob::RestoreJob(btrfsbackup::restore::RestorePlan plan, QObject* parent)
    : KJob(parent), plan_(std::move(plan)) {
    setCapabilities(KJob::Killable);
}

RestoreJob::~RestoreJob() noexcept {
    cancellation_.request_cancel();
}

void RestoreJob::start() {
    if (started_)
        return;
    started_ = true;
    Q_EMIT description(this, i18n("Restoring backup"), {i18n("Source"), QString::fromStdString(plan_.source.filename().string())}, {i18n("Destination"), QString::fromStdString(plan_.destination.string())});
    worker_ = std::jthread([this] {
        try {
            btrfsbackup::platform::linux::restore::PosixRestoreOperations operations;
            btrfsbackup::restore::RestoreExecutor executor(operations);
            const auto result = executor.execute(plan_, cancellation_);
            QMetaObject::invokeMethod(this, [this, result] {
                setProcessedAmount(KJob::Files, result.statistics.files);
                setProcessedAmount(KJob::Bytes, result.statistics.bytes);
                finish_successfully(); }, Qt::QueuedConnection);
        } catch (const btrfsbackup::restore::RestoreError& error) {
            const auto code = error.code();
            const QString details = QString::fromUtf8(error.what());
            QMetaObject::invokeMethod(this, [this, code, details] { finish_with_restore_error(code, details); }, Qt::QueuedConnection);
        } catch (const std::exception& error) {
            const QString details = QString::fromUtf8(error.what());
            QMetaObject::invokeMethod(this, [this, details] { finish_with_unexpected_error(details); }, Qt::QueuedConnection);
        }
    });
}

bool RestoreJob::doKill() {
    cancellation_.request_cancel();
    setCapabilities(KJob::NoCapabilities);
    return false;
}

bool RestoreJob::hasRestoreError() const noexcept {
    return restore_error_code_.has_value();
}

btrfsbackup::restore::RestoreErrorCode RestoreJob::restoreErrorCode() const noexcept {
    return restore_error_code_.value_or(btrfsbackup::restore::RestoreErrorCode::CopyFailed);
}

QString RestoreJob::technicalDetails() const {
    return technical_details_;
}

void RestoreJob::finish_successfully() {
    setCapabilities(KJob::NoCapabilities);
    emitResult();
}

void RestoreJob::finish_with_restore_error(btrfsbackup::restore::RestoreErrorCode code, QString details) {
    restore_error_code_ = code;
    technical_details_ = std::move(details);
    const RestoreErrorPresentation presentation = present_restore_error(code, technical_details_);
    setCapabilities(KJob::NoCapabilities);
    setError(cancellation_.cancellation_requested() ? KJob::KilledJobError : KJob::UserDefinedError);
    setErrorText(presentation.message);
    emitResult();
}

void RestoreJob::finish_with_unexpected_error(QString details) {
    technical_details_ = std::move(details);
    const RestoreErrorPresentation presentation = present_unexpected_restore_error(technical_details_);
    setCapabilities(KJob::NoCapabilities);
    setError(cancellation_.cancellation_requested() ? KJob::KilledJobError : KJob::UserDefinedError);
    setErrorText(presentation.message);
    emitResult();
}

} // namespace btrfsbackup::kde::restore
