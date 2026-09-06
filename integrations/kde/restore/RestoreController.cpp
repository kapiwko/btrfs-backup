// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreController.hpp"

#include <BrowseSessionClient.hpp>

#include "ManagerApi.hpp"
#include "ByteFormatting.hpp"
#include "RepositoryCatalogDecoder.hpp"
#include "RestoreErrorPresentation.hpp"
#include "RestoreJob.hpp"
#include "RestoreSource.hpp"

#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KNotification>
#include <KFileCustomDialog>
#include <KFileWidget>

#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>
#include <QUuid>

#include <limits>
#include <algorithm>

#include <unistd.h>

#include <core/ManagerProtocol.hpp>
#include <restore/RestoreError.hpp>

using Qt::StringLiterals::operator""_s;

namespace btrfsbackup::kde::restore {
namespace {

class BrowseOperationPin final {
  public:
    explicit BrowseOperationPin(QString session_id)
        : session_id_(std::move(session_id)),
          lease_(btrfsbackup::kde::BrowseSessionClient{}.beginOperation(session_id_)) {
    }
    ~BrowseOperationPin() noexcept {
        if (!lease_)
            return;
        try {
            (void)btrfsbackup::kde::BrowseSessionClient{}.endOperation(session_id_, *lease_);
        } catch (...) {}
    }
    BrowseOperationPin(const BrowseOperationPin&) = delete;
    BrowseOperationPin& operator=(const BrowseOperationPin&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept {
        return lease_.has_value();
    }

  private:
    QString session_id_;
    std::optional<btrfsbackup::kde::BrowseOperationLease> lease_;
};

} // namespace

RestoreController::RestoreController(QUrl source_url, QObject* parent)
    : QObject(parent), source_url_(std::move(source_url)) {
    const auto source = parse_restore_source(source_url_);
    if (!source) {
        set_error(
            btrfsbackup::restore::RestoreErrorCode::PathInvalid,
            QStringLiteral("the selected backup URL is invalid")
        );
        return;
    }
    profile_id_ = source->profile_id;
    snapshot_id_ = source->snapshot_id;
    relative_path_ = source->relative_path;
    const QString name = relative_path_ == u"."_s ? snapshot_id_ : QFileInfo(relative_path_).fileName();
    destination_ = QDir::cleanPath(QDir::homePath() + u"/Downloads/"_s + name);
}

RestoreController::~RestoreController() noexcept {
    if (job_ != nullptr)
        job_->kill(KJob::Quietly);
    close_session();
}

QUrl RestoreController::sourceUrl() const {
    return source_url_;
}
QString RestoreController::sourceName() const {
    return relative_path_ == u"."_s ? snapshot_id_ : QFileInfo(relative_path_).fileName();
}
QString RestoreController::sourceType() const {
    return source_type_;
}
QString RestoreController::sourceIcon() const {
    return source_icon_;
}
bool RestoreController::sourceIsDirectory() const noexcept {
    return source_is_directory_;
}
QString RestoreController::sourceSize() const {
    return source_size_;
}
QString RestoreController::sourceModified() const {
    return source_modified_;
}
QString RestoreController::snapshotCreated() const {
    return snapshot_created_;
}
bool RestoreController::sourceDetailsAvailable() const noexcept {
    return !source_type_.isEmpty();
}
QString RestoreController::destination() const {
    return destination_;
}
bool RestoreController::replaceExisting() const {
    return replace_existing_;
}
QString RestoreController::errorText() const {
    return error_text_;
}
QString RestoreController::errorCode() const {
    return error_code_;
}
QString RestoreController::errorTechnicalDetails() const {
    return error_technical_details_;
}
bool RestoreController::busy() const {
    return busy_;
}
bool RestoreController::completed() const {
    return completed_;
}
bool RestoreController::checkingSpace() const noexcept {
    return checking_space_;
}
qulonglong RestoreController::restoredFiles() const noexcept {
    return restored_files_;
}
qulonglong RestoreController::restoredBytes() const noexcept {
    return restored_bytes_;
}
QString RestoreController::restoredSize() const {
    constexpr auto maximum = static_cast<qulonglong>(std::numeric_limits<qint64>::max());
    return btrfsbackup::kde::format_byte_size(
        static_cast<qint64>(restored_bytes_ > maximum ? maximum : restored_bytes_)
    );
}
double RestoreController::progress() const noexcept {
    return !checking_space_ && source_size_bytes_ > 0
        ? std::min(1.0, static_cast<double>(progress_bytes_) / static_cast<double>(source_size_bytes_))
        : -1.0;
}
QString RestoreController::transferredSize() const {
    const QString transferred = btrfsbackup::kde::format_byte_size(static_cast<qint64>(progress_bytes_));
    return source_size_bytes_ > 0
        ? i18n("%1 of %2", transferred, source_size_)
        : transferred;
}
QString RestoreController::transferSpeed() const {
    return progress_speed_ > 0
        ? i18n("%1/s", btrfsbackup::kde::format_byte_size(static_cast<qint64>(progress_speed_)))
        : QString{};
}
QString RestoreController::currentItem() const {
    return current_item_;
}

void RestoreController::setDestination(const QString& value) {
    const QString normalized = QDir::cleanPath(value);
    if (destination_ == normalized)
        return;
    destination_ = normalized;
    replace_existing_ = false;
    plan_.reset();
    Q_EMIT planChanged();
}

void RestoreController::setReplaceExisting(bool value) {
    if (replace_existing_ == value)
        return;
    replace_existing_ = value;
    plan_.reset();
    Q_EMIT planChanged();
}

void RestoreController::ensure_source_open() {
    if (!catalog_) {
        const auto session = btrfsbackup::kde::BrowseSessionClient{}.open(profile_id_);
        if (!session || session->profile_id != profile_id_)
            throw std::runtime_error("could not open an authorized backup browsing session");
        session_id_ = session->session_id;
        const BrowseOperationPin root_pin(session_id_);
        if (!root_pin)
            throw std::runtime_error("could not pin the backup browsing session");
        const auto repository = btrfsbackup::kde::BrowseSessionClient{}.inspectRepository(session_id_);
        if (!repository)
            throw std::runtime_error("could not inspect the backup repository");
        catalog_.emplace(RepositoryCatalogDecoder{}.decode(*repository, "/"));
        const auto& snapshot = catalog_->snapshot(snapshot_id_.toStdString());
        snapshot_created_ = QLocale{}.toString(
            QDateTime::fromSecsSinceEpoch(std::chrono::duration_cast<std::chrono::seconds>(snapshot.created_at.time_since_epoch()).count()).toLocalTime(),
            QLocale::ShortFormat
        );
        std::filesystem::path entry_path = snapshot.repository_path.value();
        if (relative_path_ != u"."_s)
            entry_path /= relative_path_.toStdString();
        btrfsbackup::kde::BrowseSessionClient client;
        const auto metadata_payload = client.inspectEntry(
            session_id_,
            QString::fromStdString(entry_path.string())
        );
        const QJsonObject metadata = metadata_payload
            ? QJsonDocument::fromJson(metadata_payload->toUtf8()).object()
            : QJsonObject{};
        const QString kind = metadata.value(u"kind"_s).toString();
        if (metadata.value(u"schemaVersion"_s).toInt() != 1 ||
            (kind != u"file"_s && kind != u"directory"_s) ||
            !metadata.value(u"size"_s).isDouble() || !metadata.value(u"modifiedAt"_s).isDouble())
            throw std::runtime_error("could not inspect the selected backup entry");
        source_type_ = kind == u"directory"_s ? i18n("Folder") : i18n("File");
        source_is_directory_ = kind == u"directory"_s;
        source_size_bytes_ = source_is_directory_ ? 0 : metadata.value(u"size"_s).toInteger();
        source_icon_ = source_is_directory_
            ? u"folder"_s
            : QMimeDatabase{}.mimeTypeForFile(sourceName(), QMimeDatabase::MatchExtension).iconName();
        if (source_icon_.isEmpty())
            source_icon_ = u"text-x-generic"_s;
        source_size_ = kind == u"directory"_s
            ? i18n("Calculated during restore")
            : btrfsbackup::kde::format_byte_size(metadata.value(u"size"_s).toInteger());
        source_modified_ = QLocale{}.toString(
            QDateTime::fromSecsSinceEpoch(metadata.value(u"modifiedAt"_s).toInteger()).toLocalTime(),
            QLocale::ShortFormat
        );
        const QDBusUnixFileDescriptor entry = client.openEntry(
            session_id_,
            QString::fromStdString(entry_path.string())
        );
        if (!entry.isValid())
            throw std::runtime_error("could not open the selected backup entry");
        source_entry_.reset(::dup(entry.fileDescriptor()));
        if (!source_entry_.valid())
            throw std::runtime_error("could not retain the selected backup entry");
        Q_EMIT sourceDetailsChanged();
    } else {
        const auto lease = btrfsbackup::kde::BrowseSessionClient{}.renew(session_id_);
        if (!lease || lease->session_id != session_id_ || lease->profile_id != profile_id_)
            throw std::runtime_error("could not renew the backup browsing session");
    }
}

bool RestoreController::prepare_plan() {
    clear_error();
    completed_ = false;
    restored_files_ = 0;
    restored_bytes_ = 0;
    try {
        if (profile_id_.isEmpty() || snapshot_id_.isEmpty() || !QDir::isAbsolutePath(destination_))
            throw std::runtime_error("restore source or destination is invalid");
        ensure_source_open();
        const BrowseOperationPin planning_pin(session_id_);
        if (!planning_pin)
            throw std::runtime_error("could not pin the backup browsing session");
        btrfsbackup::restore::RestorePlanner planner;
        plan_ = planner.plan_from_pinned_source(
            *catalog_,
            {
                .transaction_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                .snapshot_id = snapshot_id_.toStdString(),
                .source_path = btrfsbackup::restore::RelativeRestorePath{relative_path_.toStdString()},
                .destination = destination_.toStdString(),
                .kind = btrfsbackup::restore::RestoreKind::Files,
                .existing_destination = replace_existing_ ? btrfsbackup::restore::ExistingDestinationPolicy::Replace : btrfsbackup::restore::ExistingDestinationPolicy::Fail,
            },
            std::filesystem::path{"/proc/self/fd"} / std::to_string(source_entry_.get())
        );
        Q_EMIT stateChanged();
        return true;
    } catch (const btrfsbackup::restore::RestoreError& error) {
        if (error.code() == btrfsbackup::restore::RestoreErrorCode::DestinationExists && !replace_existing_) {
            error_text_.clear();
            plan_.reset();
            Q_EMIT stateChanged();
            Q_EMIT overwriteConfirmationRequested(destination_);
            return false;
        }
        set_error(error.code(), QString::fromUtf8(error.what()));
        if (!source_entry_.valid())
            close_session();
        plan_.reset();
        Q_EMIT stateChanged();
        return false;
    } catch (const std::exception& error) {
        set_unexpected_error(QString::fromUtf8(error.what()));
        if (!source_entry_.valid())
            close_session();
        plan_.reset();
        Q_EMIT stateChanged();
        return false;
    }
}

void RestoreController::loadDetails() {
    if (profile_id_.isEmpty() || snapshot_id_.isEmpty())
        return;
    clear_error();
    try {
        ensure_source_open();
        Q_EMIT stateChanged();
    } catch (const btrfsbackup::restore::RestoreError& error) {
        close_session();
        set_error(error.code(), QString::fromUtf8(error.what()));
        Q_EMIT stateChanged();
    } catch (const std::exception& error) {
        close_session();
        set_unexpected_error(QString::fromUtf8(error.what()));
        Q_EMIT stateChanged();
    }
}

void RestoreController::chooseDestination() {
    const QFileInfo current(destination_);
    KFileCustomDialog dialog(QUrl::fromLocalFile(current.absolutePath()));
    dialog.setWindowTitle(i18n("Choose restore destination"));
    dialog.setOperationMode(KFileWidget::Opening);
    dialog.fileWidget()->setMode(KFile::Directory | KFile::LocalOnly);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QUrl selected = dialog.fileWidget()->selectedUrl();
    if (selected.isLocalFile())
        setDestination(QDir(selected.toLocalFile()).filePath(sourceName()));
}

bool RestoreController::confirmOverwrite() {
    setReplaceExisting(true);
    execute();
    return busy_;
}

void RestoreController::execute() {
    if (busy_ || !prepare_plan())
        return;
    busy_ = true;
    completed_ = false;
    clear_error();
    progress_bytes_ = 0;
    progress_speed_ = 0;
    checking_space_ = true;
    current_item_ = i18n("Estimating required space…");
    Q_EMIT progressChanged();
    execution_lease_ = btrfsbackup::kde::BrowseSessionClient{}.beginOperation(session_id_);
    if (!execution_lease_) {
        busy_ = false;
        set_unexpected_error(QStringLiteral("could not keep the backup browsing session active"));
        Q_EMIT stateChanged();
        return;
    }
    Q_EMIT stateChanged();
    job_ = new RestoreJob(*plan_, source_size_bytes_, sourceName(), this);
    tracker_.registerJob(job_);
    connect(job_, &RestoreJob::phaseChanged, this, [this](bool checking_space) {
        checking_space_ = checking_space;
        current_item_ = checking_space ? i18n("Estimating required space…") : sourceName();
        Q_EMIT progressChanged();
    });
    connect(job_, &RestoreJob::progressChanged, this, [this](qulonglong, qulonglong bytes, qulonglong speed, const QString& current_name) {
        progress_bytes_ = bytes;
        progress_speed_ = speed;
        current_item_ = current_name;
        Q_EMIT progressChanged();
    });
    connect(job_, &KJob::result, this, [this](KJob* finished) {
        tracker_.unregisterJob(finished);
        busy_ = false;
        checking_space_ = false;
        completed_ = finished->error() == KJob::NoError;
        if (completed_) {
            restored_files_ = job_->restoredFiles();
            restored_bytes_ = job_->restoredBytes();
            clear_error();
        } else if (job_->hasRestoreError()) {
            set_error(job_->restoreErrorCode(), job_->technicalDetails());
        } else {
            set_unexpected_error(job_->technicalDetails());
        }
        KNotification::event(
            completed_ ? u"restoreSuccess"_s : u"restoreFailed"_s,
            completed_ ? i18n("Restore completed") : i18n("Restore failed"),
            completed_ ? i18n("Restored to %1", destination_) : error_text_,
            u"view-history"_s
        );
        job_->deleteLater();
        job_ = nullptr;
        close_session();
        Q_EMIT stateChanged();
    });
    job_->start();
}

void RestoreController::clear_error() {
    error_text_.clear();
    error_code_.clear();
    error_technical_details_.clear();
}

void RestoreController::set_error(
    btrfsbackup::restore::RestoreErrorCode code,
    const QString& technical_details
) {
    const RestoreErrorPresentation presentation = present_restore_error(code, technical_details);
    error_text_ = presentation.message;
    error_code_ = presentation.code;
    error_technical_details_ = presentation.technical_details;
}

void RestoreController::set_unexpected_error(const QString& technical_details) {
    const RestoreErrorPresentation presentation = present_unexpected_restore_error(technical_details);
    error_text_ = presentation.message;
    error_code_ = presentation.code;
    error_technical_details_ = presentation.technical_details;
}

void RestoreController::cancel() {
    if (job_ != nullptr)
        job_->kill(KJob::EmitResult);
}

void RestoreController::openRestoredLocation() {
    if (!completed_ || !QDir::isAbsolutePath(destination_))
        return;
    const QFileInfo restored(destination_);
    if (!source_is_directory_) {
        KIO::highlightInFileManager({QUrl::fromLocalFile(restored.absoluteFilePath())});
        return;
    }
    const QString directory = restored.absoluteFilePath();
    auto* job = new KIO::OpenUrlJob(QUrl::fromLocalFile(directory), u"inode/directory"_s, this);
    job->start();
}

void RestoreController::close_session() noexcept {
    if (session_id_.isEmpty())
        return;
    source_entry_.reset();
    catalog_.reset();
    try {
        btrfsbackup::kde::BrowseSessionClient sessions;
        if (execution_lease_)
            (void)sessions.endOperation(session_id_, *execution_lease_);
        execution_lease_.reset();
        (void)sessions.close(session_id_);
    } catch (...) {}
    session_id_.clear();
}

} // namespace btrfsbackup::kde::restore
