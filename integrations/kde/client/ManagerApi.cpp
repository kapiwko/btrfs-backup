// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ManagerApi.hpp"

#include "Manager1Interface.h"

#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QDir>
#include <QFile>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <utility>

namespace btrfsbackup::kde {

ManagerEventSubscriber::ManagerEventSubscriber(QDBusConnection bus, QObject* parent)
    : QObject(parent),
      manager_(new IoGithubBtrfsbackupManager1Interface(
          QLatin1String(manager_protocol::service_name),
          QLatin1String(manager_protocol::object_path),
          bus,
          this
      )) {
    connect(
        manager_,
        &IoGithubBtrfsbackupManager1Interface::ProfilesChanged,
        this,
        &ManagerEventSubscriber::profilesChanged
    );
    connect(
        manager_,
        &IoGithubBtrfsbackupManager1Interface::StatusChanged,
        this,
        &ManagerEventSubscriber::statusChanged
    );
    connect(
        manager_,
        &IoGithubBtrfsbackupManager1Interface::HistoryChanged,
        this,
        &ManagerEventSubscriber::historyChanged
    );
    connect(
        manager_,
        &IoGithubBtrfsbackupManager1Interface::DeviceStateChanged,
        this,
        &ManagerEventSubscriber::deviceStateChanged
    );
}

ManagerClient::ManagerClient(QDBusConnection bus) : bus_(std::move(bus)) {
}

QDBusPendingCall ManagerClient::call(const QString& method, const QVariantList& arguments) const {
    return manager_call(bus_, method, arguments);
}

QDBusPendingCall ManagerClient::capabilities() const {
    return call(QLatin1String(manager_protocol::method::get_capabilities));
}

QDBusPendingCall ManagerClient::profiles() const {
    return call(QLatin1String(manager_protocol::method::list_profiles));
}

QDBusPendingCall ManagerClient::status(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::get_status), {profile_id});
}

QDBusPendingCall ManagerClient::deviceState(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::get_device_state), {profile_id});
}

QDBusPendingCall ManagerClient::history(const QString& profile_id, uint offset, uint limit) const {
    return call(QLatin1String(manager_protocol::method::get_history_sanitized), {profile_id, offset, limit});
}

QDBusPendingCall ManagerClient::startBackup(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::start_backup), {profile_id});
}

QDBusPendingCall ManagerClient::cancelBackup(const QString& profile_id, const QString& run_id) const {
    return call(QLatin1String(manager_protocol::method::cancel_backup), {profile_id, run_id});
}

QDBusPendingCall ManagerClient::ejectTarget(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::eject_target), {profile_id});
}

QDBusPendingCall ManagerClient::resolveBackupCoverage(const QString& local_path) const {
    const QByteArray encoded_path = QFile::encodeName(local_path);
    const int descriptor = ::open(encoded_path.constData(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return QDBusPendingCall::fromError(QDBusError(
            QDBusError::Failed,
            QStringLiteral("Cannot open the selected local path: %1").arg(QString::fromLocal8Bit(std::strerror(errno)))
        ));
    }
    const QDBusUnixFileDescriptor entry(descriptor);
    ::close(descriptor);
    return call(
        QLatin1String(manager_protocol::method::resolve_backup_coverage_by_fd),
        {QVariant::fromValue(entry)}
    );
}

QDBusPendingCall ManagerClient::openBrowseSession(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::open_browse_session), {profile_id});
}

QDBusPendingCall ManagerClient::renewBrowseSession(const QString& session_id) const {
    return call(QLatin1String(manager_protocol::method::renew_browse_session), {session_id});
}

QDBusPendingCall ManagerClient::beginBrowseOperation(const QString& session_id) const {
    return call(QLatin1String(manager_protocol::method::begin_browse_operation), {session_id});
}

QDBusPendingCall ManagerClient::endBrowseOperation(const QString& session_id, const QString& lease_id) const {
    return call(QLatin1String(manager_protocol::method::end_browse_operation), {session_id, lease_id});
}

QDBusPendingCall ManagerClient::closeBrowseSession(const QString& session_id) const {
    return call(QLatin1String(manager_protocol::method::close_browse_session), {session_id});
}

QDBusPendingCall ManagerClient::listBrowseDirectory(const QString& session_id, const QString& path) const {
    return call(QLatin1String(manager_protocol::method::list_browse_directory), {session_id, path});
}

QDBusPendingCall ManagerClient::listBrowseDirectoryPage(
    const QString& session_id,
    const QString& path,
    const QString& continuation_token,
    uint limit
) const {
    return call(
        QLatin1String(manager_protocol::method::list_browse_directory_page),
        {session_id, path, continuation_token, limit}
    );
}

QDBusPendingCall ManagerClient::listPreviousVersions(
    const QString& session_id,
    const QString& profile_id,
    const QString& source_id,
    const QString& relative_path,
    const QString& continuation_token,
    uint limit
) const {
    return call(
        QLatin1String(manager_protocol::method::list_previous_versions),
        {session_id, profile_id, source_id, relative_path, continuation_token, limit}
    );
}

QDBusPendingCall ManagerClient::inspectBrowseEntry(const QString& session_id, const QString& path) const {
    return call(QLatin1String(manager_protocol::method::inspect_browse_entry), {session_id, path});
}

QDBusPendingCall ManagerClient::inspectBrowseRepository(const QString& session_id) const {
    return call(QLatin1String(manager_protocol::method::inspect_browse_repository), {session_id});
}

QDBusPendingCall ManagerClient::openBrowseFile(const QString& session_id, const QString& path) const {
    return call(QLatin1String(manager_protocol::method::open_browse_file), {session_id, path});
}

QDBusPendingCall ManagerClient::openBrowseEntry(const QString& session_id, const QString& path) const {
    return call(QLatin1String(manager_protocol::method::open_browse_entry), {session_id, path});
}

QDBusPendingCall manager_call(
    const QDBusConnection& bus,
    const QString& method,
    const QVariantList& arguments
) {
    QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(manager_protocol::service_name),
        QLatin1String(manager_protocol::object_path),
        QLatin1String(manager_protocol::interface_name),
        method
    );
    message.setArguments(arguments);
    return bus.asyncCall(message);
}
} // namespace btrfsbackup::kde
