// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreJob.hpp"

#include "RestoreErrorPresentation.hpp"

#include <KLocalizedString>

#include <QMetaObject>

#include <chrono>

#include <platform/linux/restore/PosixRestoreOperations.hpp>
#include <restore/RestoreEngine.hpp>

namespace btrfsbackup::kde::restore {

RestoreJob::RestoreJob(
    btrfsbackup::restore::RestorePlan plan,
    std::uint64_t total_bytes,
    QString source_display_name,
    QObject* parent
)
    : KJob(parent), plan_(std::move(plan)), source_display_name_(std::move(source_display_name)),
      total_bytes_(total_bytes) {
    if (source_display_name_.isEmpty())
        source_display_name_ = QString::fromStdString(plan_.source.filename().string());
    setCapabilities(KJob::Killable);
    if (total_bytes_ > 0)
        setTotalAmount(KJob::Bytes, total_bytes_);
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
            const auto started = std::chrono::steady_clock::now();
            std::uint64_t last_reported_bytes = 0;
            auto last_report = started;
            const auto result = executor.execute(plan_, cancellation_, [&](const auto& progress) {
                const auto now = std::chrono::steady_clock::now();
                if (progress.statistics.bytes - last_reported_bytes < 4 * 1024 * 1024 &&
                    now - last_report < std::chrono::milliseconds{200} && progress.statistics.files == 0)
                    return;
                last_reported_bytes = progress.statistics.bytes;
                last_report = now;
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
                const std::uint64_t speed = elapsed > 0
                    ? progress.statistics.bytes * 1000 / static_cast<std::uint64_t>(elapsed)
                    : 0;
                const QString current_name = progress.current_path == plan_.source
                    ? source_display_name_
                    : QString::fromStdString(progress.current_path.filename().string());
                QMetaObject::invokeMethod(this, [this, progress, speed, current_name] {
                    setProcessedAmount(KJob::Files, progress.statistics.files);
                    setProcessedAmount(KJob::Bytes, progress.statistics.bytes);
                    emitSpeed(static_cast<unsigned long>(speed));
                    Q_EMIT progressChanged(
                        progress.statistics.files,
                        progress.statistics.bytes,
                        speed,
                        current_name
                    ); }, Qt::QueuedConnection);
            });
            QMetaObject::invokeMethod(this, [this, result] {
                restored_files_ = result.statistics.files;
                restored_bytes_ = result.statistics.bytes;
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

std::uint64_t RestoreJob::restoredFiles() const noexcept {
    return restored_files_;
}

std::uint64_t RestoreJob::restoredBytes() const noexcept {
    return restored_bytes_;
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
