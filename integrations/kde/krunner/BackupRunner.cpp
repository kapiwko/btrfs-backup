// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupRunner.hpp"

#include "ManagerApi.hpp"
#include "RunnerCommandParser.hpp"

#include <KLocalizedString>
#include <KPluginFactory>
#include <KRunner/QueryMatch>
#include <KRunner/RunnerContext>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QVariantMap>

#include <core/ManagerProtocol.hpp>

using Qt::StringLiterals::operator""_s;

K_PLUGIN_CLASS_WITH_JSON(BackupRunner, "backuprunner.json")

namespace {

std::optional<QString> payload(QDBusPendingCall call) {
    call.waitForFinished();
    QDBusPendingReply<QString> reply(call);
    return reply.isError() ? std::nullopt : std::optional<QString>{reply.value()};
}

QString operation_name(btrfsbackup::kde::krunner::CommandKind kind) {
    using btrfsbackup::kde::krunner::CommandKind;
    switch (kind) {
    case CommandKind::Start: return u"start"_s;
    case CommandKind::Status: return u"status"_s;
    case CommandKind::Browse: return u"browse"_s;
    case CommandKind::Versions: return u"versions"_s;
    case CommandKind::Eject: return u"eject"_s;
    }
    return {};
}

} // namespace

BackupRunner::BackupRunner(QObject* parent, const KPluginMetaData& metadata)
    : KRunner::AbstractRunner(parent, metadata) {
    addSyntax(i18n("Uruchom backup <profil>"), i18n("Start a backup profile"));
    addSyntax(i18n("Pokaż stan backupu"), i18n("Open backup status"));
    addSyntax(i18n("Przeglądaj kopie <profil>"), i18n("Browse backup snapshots"));
    addSyntax(i18n("Znajdź poprzednie wersje <plik>"), i18n("Find previous versions of a local file"));
    addSyntax(i18n("Odłącz nośnik <profil>"), i18n("Safely disconnect a backup target"));
}

void BackupRunner::match(KRunner::RunnerContext& context) {
    const auto command = btrfsbackup::kde::krunner::parse_command(context.query());
    if (!command)
        return;
    const auto capabilities_payload = payload(btrfsbackup::kde::manager_call(
        QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::get_capabilities)
    ));
    const auto capabilities = capabilities_payload ? btrfsbackup::kde::parse_capabilities(*capabilities_payload) : std::nullopt;
    if (!capabilities || capabilities->api_major != btrfsbackup::manager_protocol::api_major)
        return;
    QString required_feature;
    using btrfsbackup::kde::krunner::CommandKind;
    switch (command->kind) {
    case CommandKind::Start: required_feature = QLatin1String(btrfsbackup::manager_protocol::feature::start_backup); break;
    case CommandKind::Status: required_feature = QLatin1String(btrfsbackup::manager_protocol::feature::status); break;
    case CommandKind::Browse:
    case CommandKind::Versions: required_feature = QLatin1String(btrfsbackup::manager_protocol::feature::browse_backups); break;
    case CommandKind::Eject: required_feature = QLatin1String(btrfsbackup::manager_protocol::feature::eject_target); break;
    }
    if (!capabilities->features.contains(required_feature))
        return;
    if (command->kind == btrfsbackup::kde::krunner::CommandKind::Status ||
        command->kind == btrfsbackup::kde::krunner::CommandKind::Versions) {
        KRunner::QueryMatch result(this);
        result.setIconName(command->kind == btrfsbackup::kde::krunner::CommandKind::Status
            ? u"drive-harddisk-symbolic"_s : u"view-history"_s);
        result.setText(command->kind == btrfsbackup::kde::krunner::CommandKind::Status
            ? i18n("Show backup status") : i18n("Find previous backup versions"));
        result.setSubtext(command->kind == btrfsbackup::kde::krunner::CommandKind::Status
            ? i18n("Open the backup control module") : command->argument);
        result.setData(QVariantMap{{u"operation"_s, operation_name(command->kind)}, {u"argument"_s, command->argument}});
        context.addMatch(result);
        return;
    }

    const auto profiles_payload = payload(btrfsbackup::kde::manager_call(
        QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::list_profiles)
    ));
    const auto profiles = profiles_payload ? btrfsbackup::kde::parse_profiles(*profiles_payload) : std::nullopt;
    if (!profiles)
        return;
    for (const auto& profile : *profiles) {
        if (!profile.id.contains(command->argument, Qt::CaseInsensitive) &&
            !profile.name.contains(command->argument, Qt::CaseInsensitive))
            continue;
        KRunner::QueryMatch result(this);
        result.setIconName(command->kind == btrfsbackup::kde::krunner::CommandKind::Browse
            ? u"folder-open-symbolic"_s : u"drive-harddisk-symbolic"_s);
        if (command->kind == btrfsbackup::kde::krunner::CommandKind::Start)
            result.setText(i18n("Start backup: %1", profile.name));
        else if (command->kind == btrfsbackup::kde::krunner::CommandKind::Browse)
            result.setText(i18n("Browse backups: %1", profile.name));
        else
            result.setText(i18n("Disconnect backup target: %1", profile.name));
        result.setSubtext(profile.id);
        result.setData(QVariantMap{{u"operation"_s, operation_name(command->kind)}, {u"profile"_s, profile.id}});
        context.addMatch(result);
    }
}

void BackupRunner::run(const KRunner::RunnerContext&, const KRunner::QueryMatch& match) {
    const QVariantMap data = match.data().toMap();
    const QString operation = data.value(u"operation"_s).toString();
    const QString profile = data.value(u"profile"_s).toString();
    if (operation == u"status"_s) {
        (void)QProcess::startDetached(u"systemsettings"_s, {u"kcm_btrfsbackup"_s});
    } else if (operation == u"browse"_s) {
        QUrl url;
        url.setScheme(u"btrfsbackup"_s);
        url.setPath(u"/"_s + profile);
        (void)QProcess::startDetached(u"dolphin"_s, {url.toString(QUrl::FullyEncoded)});
    } else if (operation == u"versions"_s) {
        resolve_versions(data.value(u"argument"_s).toString());
    } else {
        const QString method = operation == u"start"_s
            ? QLatin1String(btrfsbackup::manager_protocol::method::start_backup)
            : QLatin1String(btrfsbackup::manager_protocol::method::eject_target);
        auto* watcher = new QDBusPendingCallWatcher(
            btrfsbackup::kde::manager_call(QDBusConnection::systemBus(), method, {profile}), this
        );
        connect(watcher, &QDBusPendingCallWatcher::finished, watcher, &QObject::deleteLater);
    }
}

void BackupRunner::resolve_versions(const QString& value) {
    const QString local_path = QUrl(value).isLocalFile() ? QUrl(value).toLocalFile() : QFileInfo(value).absoluteFilePath();
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::manager_call(
        QDBusConnection::systemBus(), QLatin1String(btrfsbackup::manager_protocol::method::resolve_backup_coverage),
        {local_path}
    ), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        const auto coverage = reply.isError() ? std::nullopt : btrfsbackup::kde::parse_backup_coverage(reply.value());
        if (!coverage || coverage->isEmpty())
            return;
        const auto& item = coverage->front();
        QUrl url;
        url.setScheme(u"btrfsbackup"_s);
        QString path = u"/"_s + item.profile_id + u"/.versions/"_s + item.source_id;
        if (item.relative_path != u"."_s)
            path += u"/"_s + item.relative_path;
        url.setPath(path);
        (void)QProcess::startDetached(u"dolphin"_s, {url.toString(QUrl::FullyEncoded)});
    });
}

#include "BackupRunner.moc"
