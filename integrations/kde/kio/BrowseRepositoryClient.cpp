// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BrowseRepositoryClient.hpp"

#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <unistd.h>

#include <core/ManagerProtocol.hpp>

using Qt::StringLiterals::operator""_s;

namespace btrfsbackup::kde::kio {
namespace {
std::optional<RemoteEntry> parse_remote_entry(const QJsonObject& object) {
    const QJsonValue name = object.value(u"name"_s);
    const QJsonValue kind = object.value(u"kind"_s);
    const QJsonValue size = object.value(u"size"_s);
    const QJsonValue mode = object.value(u"mode"_s);
    const QJsonValue modified = object.value(u"modifiedAt"_s);
    if (!name.isString() || (kind != u"directory"_s && kind != u"file"_s) || !size.isDouble() ||
        !mode.isDouble() || !modified.isDouble())
        return std::nullopt;
    return RemoteEntry{
        name.toString(),
        kind == u"directory"_s,
        static_cast<std::uint64_t>(size.toDouble()),
        static_cast<std::uint32_t>(mode.toDouble()),
        static_cast<std::int64_t>(modified.toDouble()),
    };
}

std::optional<QString> text_reply(QDBusPendingCall call, QString& error_name) {
    call.waitForFinished();
    const QDBusPendingReply<QString> reply(call);
    error_name = reply.isError() ? reply.error().name() : QString{};
    return reply.isError() ? std::nullopt : std::optional<QString>{reply.value()};
}
} // namespace

std::optional<QList<ProfileSummary>> BrowseRepositoryClient::profiles() {
    const auto payload = text_reply(
        manager_call(QDBusConnection::systemBus(), QLatin1String(manager_protocol::method::list_profiles)),
        last_error_name_
    );
    return payload ? parse_profiles(*payload) : std::nullopt;
}

std::optional<QHash<QString, RepositorySnapshot>> BrowseRepositoryClient::snapshots(const QString& session_id) {
    BrowseSessionClient client;
    const auto payload = client.inspectRepository(session_id);
    last_error_name_ = client.lastErrorName();
    return payload ? parse_repository_snapshots(*payload) : std::nullopt;
}

std::optional<RemoteDirectoryPage> BrowseRepositoryClient::directoryPage(
    const QString& session_id,
    const QString& path,
    const QString& continuation_token
) {
    constexpr uint page_size = 512;
    BrowseSessionClient client;
    const auto payload = client.listDirectoryPage(session_id, path, continuation_token, page_size);
    last_error_name_ = client.lastErrorName();
    if (!payload) return std::nullopt;
    const QJsonObject root = QJsonDocument::fromJson(payload->toUtf8()).object();
    if (root.value(u"schemaVersion"_s).toInt() != 1 || !root.value(u"entries"_s).isArray() ||
        !root.value(u"continuationToken"_s).isString())
        return std::nullopt;
    RemoteDirectoryPage result;
    for (const QJsonValue& value : root.value(u"entries"_s).toArray()) {
        if (!value.isObject()) return std::nullopt;
        auto decoded = parse_remote_entry(value.toObject());
        if (!decoded) return std::nullopt;
        result.entries.push_back(std::move(*decoded));
    }
    result.continuation_token = root.value(u"continuationToken"_s).toString();
    return result;
}

std::optional<RemoteEntry> BrowseRepositoryClient::entry(const QString& session_id, const QString& path) {
    BrowseSessionClient client;
    const auto payload = client.inspectEntry(session_id, path);
    last_error_name_ = client.lastErrorName();
    if (!payload) return std::nullopt;
    const QJsonObject object = QJsonDocument::fromJson(payload->toUtf8()).object();
    return object.value(u"schemaVersion"_s).toInt() == 1 ? parse_remote_entry(object) : std::nullopt;
}

std::optional<PreviousVersionsPage> BrowseRepositoryClient::previousVersions(
    const QString& session_id,
    const QString& profile_id,
    const QString& source_id,
    const QString& relative_path,
    const QString& continuation_token
) {
    constexpr uint page_size = 512;
    BrowseSessionClient client;
    const auto payload = client.listPreviousVersions(
        session_id, profile_id, source_id, relative_path, continuation_token, page_size
    );
    last_error_name_ = client.lastErrorName();
    return payload ? parse_previous_versions_page(*payload) : std::nullopt;
}

SecureBrowseFile BrowseRepositoryClient::openFile(const QString& session_id, const QString& path) {
    QDBusPendingReply<QDBusUnixFileDescriptor> reply(manager_call(
        QDBusConnection::systemBus(),
        QLatin1String(manager_protocol::method::open_browse_file),
        {session_id, path}
    ));
    reply.waitForFinished();
    last_error_name_ = reply.isError() ? reply.error().name() : QString{};
    if (reply.isError() || !reply.value().isValid()) return {};
    return SecureBrowseFile(dup(reply.value().fileDescriptor()));
}

QString BrowseRepositoryClient::lastErrorName() const { return last_error_name_; }

BrowseOperationPin::BrowseOperationPin(const QString& session_id)
    : session_id_(session_id), lease_(BrowseSessionClient{}.beginOperation(session_id_)) {}

BrowseOperationPin::~BrowseOperationPin() noexcept {
    if (!lease_) return;
    try { (void)BrowseSessionClient{}.endOperation(session_id_, *lease_); } catch (...) {}
}

BrowseOperationPin::operator bool() const noexcept { return lease_.has_value(); }

} // namespace btrfsbackup::kde::kio
