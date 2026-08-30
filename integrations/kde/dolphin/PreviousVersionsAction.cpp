// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PreviousVersionsAction.hpp"

#include "ManagerApi.hpp"
#include "PreviousVersionsSelection.hpp"

#include <KFileItem>
#include <KFileItemListProperties>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KPluginFactory>

#include <QAction>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QIcon>
#include <QProcess>
#include <QWidget>

#include <core/ManagerProtocol.hpp>

using namespace Qt::StringLiterals;

K_PLUGIN_CLASS_WITH_JSON(PreviousVersionsAction, "previousversionsaction.json")

PreviousVersionsAction::PreviousVersionsAction(QObject* parent)
    : KAbstractFileItemActionPlugin(parent) {
}

QList<QAction*> PreviousVersionsAction::actions(
    const KFileItemListProperties& properties, QWidget* parent_widget
) {
    const QList<QUrl> urls = properties.urlList();
    if (urls.size() == 1 && urls.front().scheme() == u"btrfsbackup"_s) {
        const QStringList parts = urls.front().path(QUrl::FullyDecoded).split(u'/', Qt::SkipEmptyParts);
        if (parts.size() < 2 || parts.at(1) == u".versions"_s)
            return {};
        auto* restore = new QAction(
            QIcon::fromTheme(u"document-restore"_s), i18nc("@action:inmenu", "Restore to…"), parent_widget
        );
        const QString source = urls.front().toString(QUrl::FullyEncoded);
        connect(restore, &QAction::triggered, this, [source] {
            (void)QProcess::startDetached(u"btrfs-backup-kde-restore"_s, {u"--url"_s, source});
        });
        return {restore};
    }
    const bool symlink = urls.size() == 1 && properties.items().findByUrl(urls.front()).isLink();
    if (!btrfsbackup::kde::dolphin::can_offer_previous_versions(urls, symlink))
        return {};
    auto* action = new QAction(
        QIcon::fromTheme(u"view-history"_s), i18nc("@action:inmenu", "Previous backup versions…"), parent_widget
    );
    const QString local_path = urls.front().toLocalFile();
    connect(action, &QAction::triggered, this, [this, local_path, parent_widget] {
        resolve_and_open(local_path, parent_widget);
    });
    return {action};
}

void PreviousVersionsAction::resolve_and_open(const QString& local_path, QWidget* parent_widget) {
    Q_UNUSED(parent_widget)
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::manager_call(
        QDBusConnection::systemBus(),
        QLatin1String(btrfsbackup::manager_protocol::method::resolve_backup_coverage),
        {local_path}
    ), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError())
            return;
        const auto coverage = btrfsbackup::kde::parse_backup_coverage(reply.value());
        if (!coverage || coverage->isEmpty())
            return;
        const auto& selected = coverage->front();
        QUrl url;
        url.setScheme(u"btrfsbackup"_s);
        QString path = u"/"_s + selected.profile_id + u"/.versions/"_s + selected.source_id;
        if (selected.relative_path != u"."_s)
            path += u"/"_s + selected.relative_path;
        url.setPath(path);
        auto* job = new KIO::OpenUrlJob(url, u"inode/directory"_s, this);
        job->start();
    });
}

#include "PreviousVersionsAction.moc"
