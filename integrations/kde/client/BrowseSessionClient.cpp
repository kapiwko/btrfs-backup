// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BrowseSessionClient.hpp"

#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>

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

bool BrowseSessionClient::setActive(const QString& session_id, bool active) const {
    return payload(manager_.setBrowseSessionActive(session_id, active)).has_value();
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
