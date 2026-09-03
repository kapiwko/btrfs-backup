// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TargetStatusParser.hpp"

#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>

#include <optional>

#include <core/ManagerProtocol.hpp>

class IoGithubBtrfsbackupManager1Interface;

namespace btrfsbackup::kde {

struct ManagerCapabilities {
    int api_major = -1;
    int public_status_schema_version = -1;
    int history_schema_version = -1;
    QSet<QString> features;
};

struct ProfileSourceSummary {
    QString id;
    QString name;
};

struct ProfileSummary {
    QString id;
    QString name;
    bool enabled = true;
    QString target_name;
    QList<ProfileSourceSummary> sources;
    bool configuration_valid = true;
    QString configuration_error_code;
};

struct RunStatus {
    QString run_id;
    QString state;
    QString phase;
    QString activity;
    QString error_code;
    QString source_name;
    QString target_name;
    QString progress_accuracy;
    QString last_success_at;
    QString last_attempt_at;
    QString last_attempt_state;
    QString started_at;
    QString updated_at;
    bool can_cancel = false;
    qint64 bytes_processed = 0;
    qint64 bytes_total_estimated = 0;
    qint64 speed_bps = 0;
    qint64 eta_seconds = -1;
    int source_progress = -1;
    int overall_progress = -1;
    int source_index = 0;
    int source_count = 0;
};

struct HistoryEntry {
    QString state;
    QString error_code;
    QString source_name;
    QString target_name;
    QString started_at;
    QString finished_at;
    qint64 duration_seconds = -1;
    qint64 bytes_transferred = 0;
    int source_count = 0;
    int overall_progress = -1;
};

struct OperationResult {
    QString operation;
    QString operation_id;
    QString profile_id;
    QString run_id;
    bool accepted = false;
};

struct BrowseSessionInfo {
    QString session_id;
    QString profile_id;
    QString root_path;
    QDateTime expires_at;
    bool read_only = false;
};

struct BackupCoverage {
    QString profile_id;
    QString source_id;
    QString relative_path;
};

class ManagerEventSubscriber final : public QObject {
    Q_OBJECT

  public:
    explicit ManagerEventSubscriber(QDBusConnection bus, QObject* parent = nullptr);

  signals:
    void profilesChanged();
    void statusChanged(const QString& profile_id);
    void historyChanged(const QString& profile_id);
    void deviceStateChanged(const QString& profile_id);

  private:
    IoGithubBtrfsbackupManager1Interface* manager_;
};

class ManagerClient {
  public:
    explicit ManagerClient(QDBusConnection bus = QDBusConnection::systemBus());

    [[nodiscard]] QDBusPendingCall capabilities() const;
    [[nodiscard]] QDBusPendingCall profiles() const;
    [[nodiscard]] QDBusPendingCall status(const QString& profile_id) const;
    [[nodiscard]] QDBusPendingCall deviceState(const QString& profile_id) const;
    [[nodiscard]] QDBusPendingCall history(const QString& profile_id, uint offset, uint limit) const;
    [[nodiscard]] QDBusPendingCall startBackup(const QString& profile_id) const;
    [[nodiscard]] QDBusPendingCall cancelBackup(const QString& profile_id, const QString& run_id) const;
    [[nodiscard]] QDBusPendingCall ejectTarget(const QString& profile_id) const;
    [[nodiscard]] QDBusPendingCall resolveBackupCoverage(const QString& local_path) const;
    [[nodiscard]] QDBusPendingCall openBrowseSession(const QString& profile_id) const;
    [[nodiscard]] QDBusPendingCall renewBrowseSession(const QString& session_id) const;
    [[nodiscard]] QDBusPendingCall setBrowseSessionActive(const QString& session_id, bool active) const;
    [[nodiscard]] QDBusPendingCall closeBrowseSession(const QString& session_id) const;
    [[nodiscard]] QDBusPendingCall listBrowseDirectory(const QString& session_id, const QString& path) const;
    [[nodiscard]] QDBusPendingCall inspectBrowseEntry(const QString& session_id, const QString& path) const;
    [[nodiscard]] QDBusPendingCall inspectBrowseRepository(const QString& session_id) const;
    [[nodiscard]] QDBusPendingCall openBrowseFile(const QString& session_id, const QString& path) const;

  private:
    [[nodiscard]] QDBusPendingCall call(const QString& method, const QVariantList& arguments = {}) const;

    QDBusConnection bus_;
};

[[nodiscard]] QDBusPendingCall manager_call(
    const QDBusConnection& bus,
    const QString& method,
    const QVariantList& arguments = {}
);
[[nodiscard]] std::optional<ManagerCapabilities> parse_capabilities(const QString& payload);
[[nodiscard]] std::optional<QList<ProfileSummary>> parse_profiles(const QString& payload);
[[nodiscard]] std::optional<RunStatus> parse_status(const QString& payload);
[[nodiscard]] std::optional<QList<HistoryEntry>> parse_history(const QString& payload);
[[nodiscard]] std::optional<OperationResult> parse_operation_result(const QString& payload);
[[nodiscard]] std::optional<BrowseSessionInfo> parse_browse_session(const QString& payload);
[[nodiscard]] std::optional<QList<BackupCoverage>> parse_backup_coverage(const QString& payload);
[[nodiscard]] bool active_run_state(const QString& state);

} // namespace btrfsbackup::kde
