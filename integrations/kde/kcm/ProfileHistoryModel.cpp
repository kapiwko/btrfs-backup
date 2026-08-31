// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProfileHistoryModel.hpp"

#include <core/ManagerProtocol.hpp>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <KLocalizedString>

#include <algorithm>
#include <limits>
#include <utility>

namespace btrfsbackup::kde::kcm {

namespace {

int json_int(const QJsonObject& object, const char* key, int fallback) {
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isDouble() && value.toDouble() == value.toInt() ? value.toInt() : fallback;
}

qint64 json_int64(const QJsonObject& object, const char* key, qint64 fallback) {
    const QJsonValue value = object.value(QLatin1String(key));
    const double number = value.toDouble(-1);
    return value.isDouble() && number >= 0 && number <= static_cast<double>(std::numeric_limits<qint64>::max())
        ? static_cast<qint64>(number)
        : fallback;
}

} // namespace

ProfileHistoryModel::ProfileHistoryModel(QObject* parent)
    : QObject(parent),
      bus_(QDBusConnection::systemBus()),
      manager_events_(bus_, this) {
    connect(
        &manager_events_,
        &btrfsbackup::kde::ManagerEventSubscriber::historyChanged,
        this,
        [this](const QString& profile_id) {
            if (profile_id != profile_id_)
                return;
            if (loading_) {
                reload_queued_ = true;
                return;
            }
            loadFirstPage();
        }
    );
}

QString ProfileHistoryModel::profileId() const {
    return profile_id_;
}

void ProfileHistoryModel::setProfileId(const QString& profile_id) {
    if (profile_id_ == profile_id)
        return;
    ++generation_;
    profile_id_ = profile_id;
    entries_.clear();
    has_more_ = false;
    loading_ = false;
    reload_queued_ = false;
    clearError();
    emit profileIdChanged();
    emit entriesChanged();
    emit stateChanged();
    if (!profile_id_.isEmpty())
        loadFirstPage();
}

QVariantList ProfileHistoryModel::entries() const {
    return entries_;
}
bool ProfileHistoryModel::loading() const {
    return loading_;
}
bool ProfileHistoryModel::hasMore() const {
    return has_more_;
}
int ProfileHistoryModel::pageSize() const {
    return page_size_;
}

void ProfileHistoryModel::setPageSize(int page_size) {
    const int bounded = std::clamp(page_size, 1, 100);
    if (page_size_ == bounded)
        return;
    page_size_ = bounded;
    emit pageSizeChanged();
    if (!profile_id_.isEmpty())
        loadFirstPage();
}

QString ProfileHistoryModel::errorCode() const {
    return error_code_;
}
QString ProfileHistoryModel::errorMessage() const {
    return error_message_;
}

void ProfileHistoryModel::loadFirstPage() {
    if (profile_id_.isEmpty())
        return;
    if (loading_) {
        reload_queued_ = true;
        return;
    }
    ensureCapabilities(true);
}

void ProfileHistoryModel::loadMore() {
    if (profile_id_.isEmpty() || loading_ || !has_more_)
        return;
    ensureCapabilities(false);
}

void ProfileHistoryModel::ensureCapabilities(bool replace) {
    if (capabilities_verified_) {
        requestPage(replace);
        return;
    }

    loading_ = true;
    clearError();
    emit stateChanged();
    const quint64 request_generation = generation_;
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::manager_call(
            bus_,
            QLatin1String(btrfsbackup::manager_protocol::method::get_capabilities)
        ),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, replace](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (request_generation != generation_)
            return;
        loading_ = false;
        if (reply.isError()) {
            setError(reply.error().name(), i18n("Could not query backup manager capabilities."));
            return;
        }
        const auto capabilities = btrfsbackup::kde::parse_capabilities(reply.value());
        if (!capabilities.has_value() ||
            capabilities->api_major != btrfsbackup::manager_protocol::api_major ||
            capabilities->history_schema_version != btrfsbackup::manager_protocol::history_schema_version ||
            !capabilities->features.contains(
                QLatin1String(btrfsbackup::manager_protocol::feature::sanitized_history)
            )) {
            setError(
                QStringLiteral("manager.unsupported-history"),
                i18n("The backup manager does not provide compatible backup history.")
            );
            return;
        }
        capabilities_verified_ = true;
        requestPage(replace);
    });
}

void ProfileHistoryModel::requestPage(bool replace) {
    loading_ = true;
    clearError();
    emit stateChanged();
    const quint64 request_generation = generation_;
    const QString requested_profile = profile_id_;
    const uint offset = replace ? 0U : static_cast<uint>(entries_.size());
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::manager_call(
            bus_,
            QLatin1String(btrfsbackup::manager_protocol::method::get_history_sanitized),
            {requested_profile, offset, static_cast<uint>(page_size_)}
        ),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, requested_profile, replace](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (request_generation != generation_ || requested_profile != profile_id_)
            return;
        loading_ = false;
        if (reply.isError()) {
            setError(reply.error().name(), i18n("Could not load backup history."));
            return;
        }
        QVariantList page;
        if (!parsePage(reply.value(), page)) {
            setError(
                QStringLiteral("manager.invalid-history"),
                i18n("The backup manager returned invalid backup history.")
            );
            return;
        }
        if (replace)
            entries_ = page;
        else
            entries_.append(page);
        has_more_ = page.size() == page_size_;
        emit entriesChanged();
        emit stateChanged();
        if (std::exchange(reload_queued_, false))
            loadFirstPage();
    });
}

void ProfileHistoryModel::clearError() {
    error_code_.clear();
    error_message_.clear();
}

void ProfileHistoryModel::setError(const QString& code, const QString& message) {
    error_code_ = code;
    error_message_ = message;
    emit stateChanged();
}

bool ProfileHistoryModel::parsePage(const QString& payload, QVariantList& entries) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isArray())
        return false;

    QVariantList parsed;
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject())
            return false;
        const QJsonObject item = value.toObject();
        if (json_int(item, "schemaVersion", -1) != btrfsbackup::manager_protocol::history_schema_version)
            return false;
        const QString started_at = item.value(QStringLiteral("startedAt")).toString();
        const QString finished_at = item.value(QStringLiteral("finishedAt")).toString();
        const QDateTime started = QDateTime::fromString(started_at, Qt::ISODate);
        const QDateTime finished = QDateTime::fromString(finished_at, Qt::ISODate);
        if ((!started_at.isEmpty() && !started.isValid()) || (!finished_at.isEmpty() && !finished.isValid()))
            return false;

        QVariantMap entry;
        entry.insert(QStringLiteral("state"), item.value(QStringLiteral("state")).toString());
        entry.insert(QStringLiteral("errorCode"), item.value(QStringLiteral("errorCode")).toString());
        entry.insert(QStringLiteral("sourceName"), item.value(QStringLiteral("sourceName")).toString());
        entry.insert(QStringLiteral("targetName"), item.value(QStringLiteral("targetName")).toString());
        entry.insert(QStringLiteral("startedAt"), started_at);
        entry.insert(QStringLiteral("finishedAt"), finished_at);
        entry.insert(
            QStringLiteral("durationSeconds"),
            started.isValid() && finished.isValid()
                ? static_cast<int>(std::max<qint64>(0, started.secsTo(finished)))
                : -1
        );
        entry.insert(QStringLiteral("sourceCount"), json_int(item, "sourceCount", 0));
        entry.insert(QStringLiteral("overallProgress"), json_int(item, "overallProgress", -1));
        const qint64 bytes_transferred = json_int64(item, "bytesTransferred", 0);
        const qint64 duration_seconds = started.isValid() && finished.isValid()
            ? std::max<qint64>(0, started.secsTo(finished))
            : -1;
        entry.insert(QStringLiteral("bytesTransferred"), bytes_transferred);
        entry.insert(
            QStringLiteral("bytesTransferredText"),
            QLocale().formattedDataSize(bytes_transferred, 1, QLocale::DataSizeTraditionalFormat)
        );
        entry.insert(
            QStringLiteral("averageSpeedText"),
            duration_seconds > 0
                ? QLocale().formattedDataSize(
                      bytes_transferred / duration_seconds,
                      1,
                      QLocale::DataSizeTraditionalFormat
                  ) +
                    QStringLiteral("/s")
                : QString()
        );
        parsed.push_back(entry);
    }
    entries = std::move(parsed);
    return true;
}

} // namespace btrfsbackup::kde::kcm
