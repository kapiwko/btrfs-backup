// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BrowseSessionClient.hpp"

#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace btrfsbackup::kde {

BrowseSessionClient::BrowseSessionClient(QDBusConnection bus) : manager_(std::move(bus)) {
}

std::optional<QString> BrowseSessionClient::payload(QDBusPendingCall call) const {
    call.waitForFinished();
    const QDBusPendingReply<QString> reply(call);
    last_error_name_ = reply.isError() ? reply.error().name() : QString{};
    return reply.isError() ? std::nullopt : std::optional<QString>{reply.value()};
}

std::optional<BrowseSessionInfo> BrowseSessionClient::open(const QString& profile_id) const {
    const auto value = payload(manager_.openBrowseSession(profile_id));
    return value.has_value() ? parse_browse_session(*value) : std::nullopt;
}

std::optional<BrowseSessionInfo> BrowseSessionClient::renew(const QString& session_id) const {
    const auto value = payload(manager_.renewBrowseSession(session_id));
    return value.has_value() ? parse_browse_session(*value) : std::nullopt;
}

std::optional<BrowseOperationLease> BrowseSessionClient::beginOperation(const QString& session_id) const {
    const auto value = payload(manager_.beginBrowseOperation(session_id));
    if (!value)
        return std::nullopt;
    const QJsonDocument document = QJsonDocument::fromJson(value->toUtf8());
    if (!document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    const QString lease_id = object.value(QStringLiteral("leaseId")).toString();
    if (object.value(QStringLiteral("schemaVersion")).toInt() != 1 || lease_id.isEmpty())
        return std::nullopt;
    return BrowseOperationLease{lease_id};
}

bool BrowseSessionClient::endOperation(const QString& session_id, const BrowseOperationLease& lease) const {
    if (lease.lease_id.isEmpty())
        return false;
    return payload(manager_.endBrowseOperation(session_id, lease.lease_id)).has_value();
}

bool BrowseSessionClient::close(const QString& session_id) const {
    return payload(manager_.closeBrowseSession(session_id)).has_value();
}

std::optional<QString> BrowseSessionClient::listDirectory(const QString& session_id, const QString& path) const {
    return payload(manager_.listBrowseDirectory(session_id, path));
}

std::optional<QString> BrowseSessionClient::listDirectoryPage(
    const QString& session_id,
    const QString& path,
    const QString& continuation_token,
    uint limit
) const {
    return payload(manager_.listBrowseDirectoryPage(session_id, path, continuation_token, limit));
}

std::optional<QString> BrowseSessionClient::listPreviousVersions(
    const QString& session_id,
    const QString& profile_id,
    const QString& source_id,
    const QString& relative_path,
    const QString& continuation_token,
    uint limit
) const {
    return payload(manager_.listPreviousVersions(
        session_id,
        profile_id,
        source_id,
        relative_path,
        continuation_token,
        limit
    ));
}

std::optional<QString> BrowseSessionClient::inspectEntry(const QString& session_id, const QString& path) const {
    return payload(manager_.inspectBrowseEntry(session_id, path));
}

std::optional<QString> BrowseSessionClient::inspectRepository(const QString& session_id) const {
    return payload(manager_.inspectBrowseRepository(session_id));
}

QDBusUnixFileDescriptor BrowseSessionClient::openRoot(const QString& session_id) const {
    QDBusPendingReply<QDBusUnixFileDescriptor> reply(manager_.openBrowseRoot(session_id));
    reply.waitForFinished();
    last_error_name_ = reply.isError() ? reply.error().name() : QString{};
    return reply.isError() ? QDBusUnixFileDescriptor{} : reply.value();
}

const QString& BrowseSessionClient::lastErrorName() const noexcept {
    return last_error_name_;
}

} // namespace btrfsbackup::kde
