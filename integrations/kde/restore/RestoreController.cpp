// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreController.hpp"

#include <BrowseSessionClient.hpp>

#include "ManagerApi.hpp"
#include "RestoreJob.hpp"

#include <KLocalizedString>
#include <KNotification>
#include <KFileCustomDialog>
#include <KFileWidget>

#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <unistd.h>

#include <core/ManagerProtocol.hpp>
#include <core/RuntimeTime.hpp>
#include <restore/RestoreError.hpp>

using Qt::StringLiterals::operator""_s;

namespace btrfsbackup::kde::restore {
namespace {

QString required_string(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().isEmpty())
        throw std::runtime_error("manager returned an invalid repository catalog");
    return value.toString();
}

btrfsbackup::RuntimeTimePoint required_time(const QJsonObject& object, const QString& key) {
    const auto value = btrfsbackup::parse_utc_timestamp(required_string(object, key).toStdString());
    if (!value)
        throw std::runtime_error("manager returned an invalid repository timestamp");
    return *value;
}

btrfsbackup::restore::RepositoryCatalog parse_repository_catalog(
    const QString& payload,
    const std::filesystem::path& root_path
) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject())
        throw std::runtime_error("manager returned an invalid repository catalog");
    const QJsonObject root = document.object();
    if (root.value(u"schemaVersion"_s).toInt() != 1 || !root.value(u"features"_s).isArray() ||
        !root.value(u"snapshots"_s).isArray() || !root.value(u"generation"_s).isDouble())
        throw std::runtime_error("manager returned an unsupported repository catalog");

    std::vector<std::string> features;
    for (const QJsonValue& value : root.value(u"features"_s).toArray()) {
        if (!value.isString())
            throw std::runtime_error("manager returned an invalid repository feature");
        features.push_back(value.toString().toStdString());
    }
    std::vector<btrfsbackup::restore::CatalogSnapshot> snapshots;
    for (const QJsonValue& value : root.value(u"snapshots"_s).toArray()) {
        if (!value.isObject())
            throw std::runtime_error("manager returned an invalid repository snapshot");
        const QJsonObject snapshot = value.toObject();
        snapshots.push_back({
            .snapshot_id = required_string(snapshot, u"snapshotId"_s).toStdString(),
            .host_id = required_string(snapshot, u"hostId"_s).toStdString(),
            .profile_id = required_string(snapshot, u"profileId"_s).toStdString(),
            .source_id = required_string(snapshot, u"sourceId"_s).toStdString(),
            .repository_path = btrfsbackup::restore::RelativeRestorePath{
                required_string(snapshot, u"relativePath"_s).toStdString()
            },
            .created_at = required_time(snapshot, u"createdAt"_s),
            .uuid = required_string(snapshot, u"uuid"_s).toStdString(),
            .received_uuid = snapshot.value(u"receivedUuid"_s).toString().toStdString(),
            .parent_uuid = snapshot.value(u"parentUuid"_s).toString().toStdString(),
            .verified = snapshot.value(u"verified"_s).toBool(false),
        });
    }
    return {
        root_path,
        {
            .repository_id = required_string(root, u"repositoryId"_s).toStdString(),
            .target_filesystem_uuid = required_string(root, u"targetFilesystemUuid"_s).toStdString(),
            .created_at = required_time(root, u"createdAt"_s),
            .features = std::move(features),
        },
        static_cast<std::uint64_t>(root.value(u"generation"_s).toDouble()),
        std::move(snapshots),
    };
}

class BrowseOperationPin final {
  public:
    explicit BrowseOperationPin(QString session_id)
        : session_id_(std::move(session_id)),
          active_(btrfsbackup::kde::BrowseSessionClient{}.setActive(session_id_, true)) {
    }
    ~BrowseOperationPin() noexcept {
        if (!active_)
            return;
        try {
            (void)btrfsbackup::kde::BrowseSessionClient{}.setActive(session_id_, false);
        } catch (...) {}
    }
    BrowseOperationPin(const BrowseOperationPin&) = delete;
    BrowseOperationPin& operator=(const BrowseOperationPin&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept {
        return active_;
    }

  private:
    QString session_id_;
    bool active_ = false;
};

} // namespace

RestoreController::RestoreController(QUrl source_url, QObject* parent)
    : QObject(parent), source_url_(std::move(source_url)) {
    const QStringList parts = source_url_.path(QUrl::FullyDecoded).split(u'/', Qt::SkipEmptyParts);
    if (source_url_.scheme() != u"btrfsbackup"_s || parts.size() < 2 || parts.at(1) == u".versions"_s) {
        error_text_ = i18n("The selected backup URL is invalid.");
        return;
    }
    profile_id_ = parts.at(0);
    snapshot_id_ = parts.at(1);
    relative_path_ = parts.size() > 2 ? parts.mid(2).join(u'/') : u"."_s;
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
QString RestoreController::destination() const {
    return destination_;
}
bool RestoreController::replaceExisting() const {
    return replace_existing_;
}
QString RestoreController::planSummary() const {
    return plan_summary_;
}
QString RestoreController::errorText() const {
    return error_text_;
}
bool RestoreController::busy() const {
    return busy_;
}
bool RestoreController::completed() const {
    return completed_;
}

void RestoreController::setDestination(const QString& value) {
    const QString normalized = QDir::cleanPath(value);
    if (destination_ == normalized)
        return;
    destination_ = normalized;
    replace_existing_ = false;
    plan_.reset();
    plan_summary_.clear();
    Q_EMIT planChanged();
}

void RestoreController::setReplaceExisting(bool value) {
    if (replace_existing_ == value)
        return;
    replace_existing_ = value;
    plan_.reset();
    plan_summary_.clear();
    Q_EMIT planChanged();
}

bool RestoreController::prepare_plan() {
    error_text_.clear();
    completed_ = false;
    try {
        if (profile_id_.isEmpty() || snapshot_id_.isEmpty() || !QDir::isAbsolutePath(destination_))
            throw std::runtime_error("restore source or destination is invalid");
        if (!catalog_) {
            const auto session = btrfsbackup::kde::BrowseSessionClient{}.open(profile_id_);
            if (!session || session->profile_id != profile_id_)
                throw std::runtime_error("could not open an authorized backup browsing session");
            session_id_ = session->session_id;
            const BrowseOperationPin root_pin(session_id_);
            if (!root_pin)
                throw std::runtime_error("could not pin the backup browsing session");
            const QDBusUnixFileDescriptor root = btrfsbackup::kde::BrowseSessionClient{}.openRoot(session_id_);
            if (!root.isValid())
                throw std::runtime_error("could not open the backup browsing root");
            session_root_.reset(::dup(root.fileDescriptor()));
            if (!session_root_.valid())
                throw std::runtime_error("could not retain the backup browsing root");
            const auto repository = btrfsbackup::kde::BrowseSessionClient{}.inspectRepository(session_id_);
            if (!repository)
                throw std::runtime_error("could not inspect the backup repository");
            catalog_.emplace(parse_repository_catalog(
                *repository,
                std::filesystem::path{"/proc/self/fd"} / std::to_string(session_root_.get())
            ));
        } else {
            const auto lease = btrfsbackup::kde::BrowseSessionClient{}.renew(session_id_);
            if (!lease || lease->session_id != session_id_ || lease->profile_id != profile_id_)
                throw std::runtime_error("could not renew the backup browsing session");
        }
        const BrowseOperationPin planning_pin(session_id_);
        if (!planning_pin)
            throw std::runtime_error("could not pin the backup browsing session");
        btrfsbackup::restore::RestorePlanner planner;
        plan_ = planner.plan(*catalog_, {
                                            .transaction_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                                            .snapshot_id = snapshot_id_.toStdString(),
                                            .source_path = btrfsbackup::restore::RelativeRestorePath{relative_path_.toStdString()},
                                            .destination = destination_.toStdString(),
                                            .kind = btrfsbackup::restore::RestoreKind::Files,
                                            .existing_destination = replace_existing_ ? btrfsbackup::restore::ExistingDestinationPolicy::Replace : btrfsbackup::restore::ExistingDestinationPolicy::Fail,
                                        });
        plan_summary_ = plan_->destination_exists
            ? i18n("Replace %1, preserve metadata, then verify the restored data.", destination_)
            : i18n("Restore to %1, preserve metadata, then verify the restored data.", destination_);
        Q_EMIT planChanged();
        Q_EMIT stateChanged();
        return true;
    } catch (const btrfsbackup::restore::RestoreError& error) {
        if (error.code() == btrfsbackup::restore::RestoreErrorCode::DestinationExists && !replace_existing_) {
            error_text_.clear();
            plan_.reset();
            plan_summary_.clear();
            Q_EMIT planChanged();
            Q_EMIT stateChanged();
            Q_EMIT overwriteConfirmationRequested(destination_);
            return false;
        }
        error_text_ = QString::fromUtf8(error.what());
        plan_.reset();
        plan_summary_.clear();
        Q_EMIT planChanged();
        Q_EMIT stateChanged();
        return false;
    } catch (const std::exception& error) {
        error_text_ = QString::fromUtf8(error.what());
        plan_.reset();
        plan_summary_.clear();
        Q_EMIT planChanged();
        Q_EMIT stateChanged();
        return false;
    }
}

bool RestoreController::preview() {
    return prepare_plan();
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
    return prepare_plan();
}

void RestoreController::execute() {
    if (busy_ || !prepare_plan())
        return;
    busy_ = true;
    completed_ = false;
    error_text_.clear();
    if (!btrfsbackup::kde::BrowseSessionClient{}.setActive(session_id_, true)) {
        busy_ = false;
        error_text_ = i18n("Could not keep the backup browsing session active.");
        Q_EMIT stateChanged();
        return;
    }
    Q_EMIT stateChanged();
    job_ = new RestoreJob(*plan_, this);
    tracker_.registerJob(job_);
    connect(job_, &KJob::result, this, [this](KJob* finished) {
        tracker_.unregisterJob(finished);
        busy_ = false;
        completed_ = finished->error() == KJob::NoError;
        error_text_ = completed_ ? QString{} : finished->errorString();
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

void RestoreController::cancel() {
    if (job_ != nullptr)
        job_->kill(KJob::EmitResult);
}

void RestoreController::close_session() noexcept {
    if (session_id_.isEmpty())
        return;
    session_root_.reset();
    catalog_.reset();
    try {
        btrfsbackup::kde::BrowseSessionClient sessions;
        (void)sessions.setActive(session_id_, false);
        (void)sessions.close(session_id_);
    } catch (...) {}
    session_id_.clear();
}

} // namespace btrfsbackup::kde::restore
