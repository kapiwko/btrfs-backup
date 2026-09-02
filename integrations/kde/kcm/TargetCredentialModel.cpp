// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TargetCredentialModel.hpp"

#include <ManagerApi.hpp>
#include <core/ManagerProtocol.hpp>

#include <KLocalizedString>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace btrfsbackup::kde::kcm {

namespace {

QDBusUnixFileDescriptor secret_descriptor(const QString& secret) {
    QByteArray bytes = secret.toUtf8();
    if (bytes.isEmpty() || bytes.size() > 4096)
        return {};
    const int descriptor = ::memfd_create("btrfs-backup-kcm-secret", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (descriptor < 0)
        return {};
    qsizetype written = 0;
    while (written < bytes.size()) {
        const ssize_t result = ::write(descriptor, bytes.constData() + written, static_cast<std::size_t>(bytes.size() - written));
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0) {
            bytes.fill('\0');
            ::close(descriptor);
            return {};
        }
        written += result;
    }
    bytes.fill('\0');
    if (::fcntl(descriptor, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) < 0) {
        ::close(descriptor);
        return {};
    }
    (void)::lseek(descriptor, 0, SEEK_SET);
    QDBusUnixFileDescriptor result(descriptor);
    ::close(descriptor);
    return result;
}

} // namespace

TargetCredentialModel::TargetCredentialModel(QObject* parent)
    : QObject(parent), bus_(QDBusConnection::systemBus()) {
}

QString TargetCredentialModel::profileId() const {
    return profile_id_;
}
QVariantList TargetCredentialModel::credentials() const {
    return credentials_;
}
bool TargetCredentialModel::busy() const {
    return busy_;
}
QString TargetCredentialModel::errorCode() const {
    return error_code_;
}
QString TargetCredentialModel::errorMessage() const {
    return error_message_;
}

void TargetCredentialModel::load(const QString& profile_id) {
    if (busy_ || profile_id.isEmpty())
        return;
    profile_id_ = profile_id;
    emit stateChanged();
    request(RequestKind::List, QLatin1String(manager_protocol::method::list_target_credentials), {profile_id_});
}

void TargetCredentialModel::addPassphrase(
    const QString& authorization_secret,
    const QString& new_secret,
    const QString& confirmation,
    const QString& label
) {
    if (busy_ || profile_id_.isEmpty())
        return;
    if (new_secret != confirmation) {
        setError(QStringLiteral("credentials.passphrases-differ"), i18n("The passphrases do not match."));
        return;
    }
    const auto authorization = secret_descriptor(authorization_secret);
    const auto added = secret_descriptor(new_secret);
    if (!authorization.isValid() || !added.isValid()) {
        setError(QStringLiteral("credentials.invalid-secret"), i18n("A passphrase must contain between 1 and 4096 bytes."));
        return;
    }
    request(
        RequestKind::Mutation,
        QLatin1String(manager_protocol::method::add_target_passphrase),
        {profile_id_, QVariant::fromValue(authorization), QVariant::fromValue(added), label.trimmed()}
    );
}

void TargetCredentialModel::generateKey(
    const QString& authorization_secret,
    const QString& label,
    bool automatic
) {
    if (busy_ || profile_id_.isEmpty())
        return;
    const auto authorization = secret_descriptor(authorization_secret);
    if (!authorization.isValid()) {
        setError(QStringLiteral("credentials.invalid-secret"), i18n("A passphrase must contain between 1 and 4096 bytes."));
        return;
    }
    request(
        RequestKind::Mutation,
        QLatin1String(manager_protocol::method::generate_target_key),
        {profile_id_, QVariant::fromValue(authorization), label.trimmed(), automatic}
    );
}

void TargetCredentialModel::addKey(
    const QString& authorization_secret,
    const QUrl& key_file,
    const QString& label,
    bool automatic
) {
    if (busy_ || profile_id_.isEmpty())
        return;
    const auto authorization = secret_descriptor(authorization_secret);
    QFile key(key_file.toLocalFile());
    if (!authorization.isValid() || !key.open(QIODevice::ReadOnly) || key.size() < 1 || key.size() > 4096) {
        setError(QStringLiteral("credentials.invalid-key"), i18n("Select a key file between 1 and 4096 bytes and enter a valid passphrase."));
        return;
    }
    const QDBusUnixFileDescriptor key_descriptor(key.handle());
    request(
        RequestKind::Mutation,
        QLatin1String(manager_protocol::method::add_target_key),
        {profile_id_, QVariant::fromValue(authorization), QVariant::fromValue(key_descriptor), label.trimmed(), automatic}
    );
}

void TargetCredentialModel::removeCredential(
    const QString& credential_id,
    const QString& authorization_secret
) {
    if (busy_ || profile_id_.isEmpty() || credential_id.isEmpty())
        return;
    const auto authorization = secret_descriptor(authorization_secret);
    if (!authorization.isValid()) {
        setError(QStringLiteral("credentials.invalid-secret"), i18n("A passphrase must contain between 1 and 4096 bytes."));
        return;
    }
    request(
        RequestKind::Mutation,
        QLatin1String(manager_protocol::method::remove_target_credential),
        {profile_id_, credential_id, QVariant::fromValue(authorization)}
    );
}

void TargetCredentialModel::clearError() {
    setError({}, {});
}

void TargetCredentialModel::request(RequestKind kind, const QString& method, const QVariantList& arguments) {
    busy_ = true;
    setError({}, {});
    emit stateChanged();
    auto* watcher = new QDBusPendingCallWatcher(manager_call(bus_, method, arguments), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, kind](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        busy_ = false;
        if (reply.isError()) {
            const QString error_name = reply.error().name();
            const QString error_message = error_name.endsWith(QStringLiteral(".TargetUnavailable"))
                ? i18n("The backup device is disconnected. Connect it to view its unlocking methods.")
                : reply.error().message();
            setError(error_name, error_message);
            emit stateChanged();
            return;
        }
        if (!applyCredentials(reply.value())) {
            setError(QStringLiteral("manager.invalid-response"), i18n("The backup manager returned an invalid credential response."));
            emit stateChanged();
            return;
        }
        if (kind == RequestKind::Mutation)
            emit credentialsChanged();
        emit stateChanged();
    });
}

bool TargetCredentialModel::applyCredentials(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isArray())
        return false;
    QVariantList result;
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject())
            return false;
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("schemaVersion")).toInt(-1) != manager_protocol::target_credentials_schema_version ||
            item.value(QStringLiteral("id")).toString().isEmpty() || item.value(QStringLiteral("keyslot")).toInt(-1) < 0)
            return false;
        result.push_back(item.toVariantMap());
    }
    credentials_ = std::move(result);
    emit credentialsChanged();
    return true;
}

void TargetCredentialModel::setError(const QString& code, const QString& message) {
    if (error_code_ == code && error_message_ == message)
        return;
    error_code_ = code;
    error_message_ = message;
    emit stateChanged();
}

} // namespace btrfsbackup::kde::kcm
