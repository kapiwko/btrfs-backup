// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>

#include <optional>

namespace btrfsbackup::kde {

inline constexpr auto manager_service = "io.github.btrfsbackup.Manager1";
inline constexpr auto manager_path = "/io/github/btrfsbackup/Manager1";
inline constexpr auto manager_interface = "io.github.btrfsbackup.Manager1";

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
[[nodiscard]] bool active_run_state(const QString& state);

} // namespace btrfsbackup::kde
