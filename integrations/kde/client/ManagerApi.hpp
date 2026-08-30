// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TargetStatusParser.hpp"

#include <QDBusConnection>
#include <QDBusPendingCall>
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
    QSet<QString> features;
};

struct ProfileSummary {
    QString id;
    QString name;
    QString target_name;
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
    bool can_cancel = false;
    qint64 speed_bps = 0;
    qint64 eta_seconds = -1;
    int source_progress = -1;
    int overall_progress = -1;
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

[[nodiscard]] QDBusPendingCall manager_call(
    const QDBusConnection& bus,
    const QString& method,
    const QVariantList& arguments = {}
);
[[nodiscard]] std::optional<ManagerCapabilities> parse_capabilities(const QString& payload);
[[nodiscard]] std::optional<QList<ProfileSummary>> parse_profiles(const QString& payload);
[[nodiscard]] std::optional<RunStatus> parse_status(const QString& payload);
[[nodiscard]] bool active_run_state(const QString& state);

} // namespace btrfsbackup::kde
