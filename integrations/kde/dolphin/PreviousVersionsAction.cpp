// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PreviousVersionsAction.hpp"

#include "ManagerApi.hpp"
#include "PreviousVersionsSelection.hpp"

#include <KFileItem>
#include <KFileItemListProperties>
#include <KIO/OpenUrlJob>
#include <KJob>
#include <KLocalizedString>
#include <KMessageBox>
#include <KPluginFactory>

#include <QAction>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QIcon>
#include <QPointer>
#include <QProcess>
#include <QWidget>

#include <optional>

#include <core/ManagerProtocol.hpp>

using Qt::StringLiterals::operator""_s;

K_PLUGIN_CLASS_WITH_JSON(PreviousVersionsAction, "previousversionsaction.json")

namespace {

constexpr char translation_domain[] = "btrfs-backup-dolphin";

void show_service_error(QWidget* parent) {
    KMessageBox::error(
        parent,
        i18nd(translation_domain, "Could not contact the backup service or access backup versions."),
        i18nd(translation_domain, "Previous Backup Versions"),
        KMessageBox::Notify | KMessageBox::PlainText
    );
}

void show_no_versions(QWidget* parent) {
    KMessageBox::information(
        parent,
        i18nd(translation_domain, "No backup versions were found for the selected path."),
        i18nd(translation_domain, "Previous Backup Versions"),
        {},
        KMessageBox::Notify | KMessageBox::PlainText
    );
}

void show_restore_start_error(QWidget* parent) {
    KMessageBox::error(
        parent,
        i18nd(translation_domain, "Could not start the restore application."),
        i18nd(translation_domain, "Restore Backup Version"),
        KMessageBox::Notify | KMessageBox::PlainText
    );
}

} // namespace

PreviousVersionsAction::PreviousVersionsAction(QObject* parent)
    : KAbstractFileItemActionPlugin(parent) {
}

QList<QAction*> PreviousVersionsAction::actions(
    const KFileItemListProperties& properties,
    QWidget* parent_widget
) {
    const QList<QUrl> urls = properties.urlList();
    if (urls.size() == 1 && urls.front().scheme() == u"btrfsbackup"_s) {
        const QStringList parts = urls.front().path(QUrl::FullyDecoded).split(u'/', Qt::SkipEmptyParts);
        if (parts.size() < 2 || parts.at(1) == u".versions"_s)
            return {};
        auto* restore = new QAction(
            QIcon::fromTheme(u"document-restore"_s),
            i18ndc(translation_domain, "@action:inmenu", "Restore to…"),
            parent_widget
        );
        const QString source = urls.front().toString(QUrl::FullyEncoded);
        const QPointer<QWidget> parent = parent_widget;
        connect(restore, &QAction::triggered, this, [source, parent] {
            if (!QProcess::startDetached(u"btrfs-backup-kde-restore"_s, {u"--url"_s, source}))
                show_restore_start_error(parent.data());
        });
        return {restore};
    }
    const bool symlink = urls.size() == 1 && properties.items().findByUrl(urls.front()).isLink();
    if (!btrfsbackup::kde::dolphin::can_offer_previous_versions(urls, symlink))
        return {};
    auto* action = new QAction(
        QIcon::fromTheme(u"view-history"_s),
        i18ndc(translation_domain, "@action:inmenu", "Previous backup versions…"),
        parent_widget
    );
    const QString local_path = urls.front().toLocalFile();
    connect(action, &QAction::triggered, this, [this, local_path, parent_widget] {
        resolve_and_open(local_path, parent_widget);
    });
    return {action};
}

void PreviousVersionsAction::resolve_and_open(const QString& local_path, QWidget* parent_widget) {
    const QPointer<QWidget> parent = parent_widget;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::resolve_backup_coverage), {local_path}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, parent](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        const bool request_succeeded = !reply.isError();
        const auto coverage = request_succeeded
            ? btrfsbackup::kde::parse_backup_coverage(reply.value())
            : std::optional<QList<btrfsbackup::kde::BackupCoverage>>{};
        const auto outcome = btrfsbackup::kde::dolphin::classify_previous_versions(
            request_succeeded,
            coverage.has_value(),
            coverage.has_value() && !coverage->isEmpty()
        );
        if (outcome == btrfsbackup::kde::dolphin::PreviousVersionsOutcome::ServiceError) {
            show_service_error(parent.data());
            return;
        }
        if (outcome == btrfsbackup::kde::dolphin::PreviousVersionsOutcome::NotFound) {
            show_no_versions(parent.data());
            return;
        }
        const auto& selected = coverage->front();
        QUrl url;
        url.setScheme(u"btrfsbackup"_s);
        QString path = u"/"_s + selected.profile_id + u"/.versions/"_s + selected.source_id;
        if (selected.relative_path != u"."_s)
            path += u"/"_s + selected.relative_path;
        url.setPath(path);
        auto* job = new KIO::OpenUrlJob(url, u"inode/directory"_s, this);
        connect(job, &KJob::result, this, [parent](KJob* completed) {
            if (completed->error() != 0)
                show_service_error(parent.data());
        });
        job->start();
    });
}

#include "PreviousVersionsAction.moc"
