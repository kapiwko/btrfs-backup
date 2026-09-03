// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupHistoryModel.hpp"

#include <core/ManagerProtocol.hpp>

#include <ManagerApi.hpp>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLocale>
#include <QVariantMap>

#include <algorithm>
#include <utility>

BackupHistoryModel::BackupHistoryModel(QObject* parent)
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

QString BackupHistoryModel::profileId() const {
    return profile_id_;
}

void BackupHistoryModel::setProfileId(const QString& profile_id) {
    if (profile_id_ == profile_id)
        return;
    ++generation_;
    profile_id_ = profile_id;
    entries_.clear();
    error_code_.clear();
    has_more_ = false;
    loading_ = false;
    reload_queued_ = false;
    emit profileIdChanged();
    emit changed();
    emit stateChanged();
    if (!profile_id_.isEmpty())
        loadFirstPage();
}

QVariantList BackupHistoryModel::entries() const {
    return entries_;
}

bool BackupHistoryModel::loading() const {
    return loading_;
}

bool BackupHistoryModel::hasMore() const {
    return has_more_;
}

int BackupHistoryModel::pageSize() const {
    return page_size_;
}

void BackupHistoryModel::setPageSize(int page_size) {
    const int bounded = std::clamp(page_size, 1, 100);
    if (page_size_ == bounded)
        return;
    page_size_ = bounded;
    emit pageSizeChanged();
    if (!profile_id_.isEmpty())
        loadFirstPage();
}

QString BackupHistoryModel::errorCode() const {
    return error_code_;
}

bool BackupHistoryModel::apply(const QString& payload) {
    QVariantList entries;
    if (!parseEntries(payload, entries))
        return false;
    entries_ = std::move(entries);
    error_code_.clear();
    emit changed();
    emit stateChanged();
    return true;
}

void BackupHistoryModel::reset() {
    ++generation_;
    entries_.clear();
    error_code_.clear();
    loading_ = false;
    has_more_ = false;
    reload_queued_ = false;
    emit changed();
    emit stateChanged();
}

void BackupHistoryModel::loadFirstPage() {
    if (profile_id_.isEmpty())
        return;
    if (loading_) {
        reload_queued_ = true;
        return;
    }
    ensureCapabilities(true);
}

void BackupHistoryModel::loadMore() {
    if (profile_id_.isEmpty() || loading_ || !has_more_)
        return;
    ensureCapabilities(false);
}

void BackupHistoryModel::ensureCapabilities(bool replace) {
    if (capabilities_verified_) {
        requestPage(replace);
        return;
    }

    loading_ = true;
    error_code_.clear();
    emit stateChanged();
    const quint64 request_generation = generation_;
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::ManagerClient{bus_}.capabilities(),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, replace](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (request_generation != generation_)
            return;
        loading_ = false;
        if (reply.isError()) {
            setError(reply.error().name());
            return;
        }
        const auto capabilities = btrfsbackup::kde::parse_capabilities(reply.value());
        if (!capabilities.has_value() ||
            capabilities->api_major != btrfsbackup::manager_protocol::api_major ||
            capabilities->history_schema_version != btrfsbackup::manager_protocol::history_schema_version ||
            !capabilities->features.contains(
                QLatin1String(btrfsbackup::manager_protocol::feature::sanitized_history)
            )) {
            setError(QStringLiteral("manager.unsupported-history"));
            return;
        }
        capabilities_verified_ = true;
        requestPage(replace);
    });
}

void BackupHistoryModel::requestPage(bool replace) {
    loading_ = true;
    error_code_.clear();
    emit stateChanged();
    const quint64 request_generation = generation_;
    const QString requested_profile = profile_id_;
    const uint offset = replace ? 0U : static_cast<uint>(entries_.size());
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::ManagerClient{bus_}.history(
            requested_profile,
            offset,
            static_cast<uint>(page_size_)
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
            setError(reply.error().name());
            return;
        }
        QVariantList page;
        if (!parseEntries(reply.value(), page)) {
            setError(QStringLiteral("manager.invalid-history"));
            return;
        }
        if (replace)
            entries_ = page;
        else
            entries_.append(page);
        has_more_ = page.size() == page_size_;
        emit changed();
        emit stateChanged();
        if (std::exchange(reload_queued_, false))
            loadFirstPage();
    });
}

void BackupHistoryModel::setError(const QString& code) {
    error_code_ = code;
    reload_queued_ = false;
    emit stateChanged();
}

bool BackupHistoryModel::parseEntries(const QString& payload, QVariantList& entries) {
    const auto history = btrfsbackup::kde::parse_history(payload);
    if (!history.has_value())
        return false;

    QVariantList parsed;
    for (const btrfsbackup::kde::HistoryEntry& item : *history) {
        QVariantMap entry;
        entry.insert(QStringLiteral("state"), item.state);
        entry.insert(QStringLiteral("errorCode"), item.error_code);
        entry.insert(QStringLiteral("sourceName"), item.source_name);
        entry.insert(QStringLiteral("targetName"), item.target_name);
        entry.insert(QStringLiteral("startedAt"), item.started_at);
        entry.insert(QStringLiteral("finishedAt"), item.finished_at);
        entry.insert(QStringLiteral("durationSeconds"), item.duration_seconds);
        entry.insert(QStringLiteral("sourceCount"), item.source_count);
        entry.insert(QStringLiteral("overallProgress"), item.overall_progress);
        entry.insert(QStringLiteral("bytesTransferred"), item.bytes_transferred);
        entry.insert(
            QStringLiteral("bytesTransferredText"),
            QLocale().formattedDataSize(item.bytes_transferred, 1, QLocale::DataSizeTraditionalFormat)
        );
        entry.insert(
            QStringLiteral("averageSpeedText"),
            item.duration_seconds > 0
                ? QLocale().formattedDataSize(
                      item.bytes_transferred / item.duration_seconds,
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
