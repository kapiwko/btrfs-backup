// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "CancellationRequestDispatcher.hpp"
#include "ManagerApi.hpp"
#include "TerminalNotificationService.hpp"

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include <cstdint>

class KUiServerV2JobTracker;

namespace btrfsbackup::kde::monitor {

class BackupProgressJob;

class BackupProgressMonitor final : public QObject {
    Q_OBJECT

  public:
    BackupProgressMonitor(
        QDBusConnection bus,
        KUiServerV2JobTracker& tracker,
        QObject* parent = nullptr
    );

    void start();

  private:
    using Profile = btrfsbackup::kde::ProfileSummary;
    using Status = btrfsbackup::kde::RunStatus;

    void connect_to_manager();
    void manager_unavailable();
    void request_profiles();
    void request_status(const Profile& profile);
    void request_cancel(const QString& profile_id, const QString& run_id);
    void apply_profiles(const QString& payload);
    void apply_status(const Profile& profile, const QString& payload);
    void create_job(const Profile& profile, const Status& status);
    void finish_job(const QString& profile_id, const Status& status);
    QDBusConnection bus_;
    btrfsbackup::kde::ManagerEventSubscriber manager_events_;
    QDBusServiceWatcher service_watcher_;
    CancellationRequestDispatcher cancellation_dispatcher_;
    TerminalNotificationService terminal_notifications_;
    KUiServerV2JobTracker& tracker_;
    QHash<QString, Profile> profiles_;
    QHash<QString, QPointer<BackupProgressJob>> jobs_;
    QSet<QString> pending_status_requests_;
    QSet<QString> queued_status_requests_;
    QSet<QString> manager_features_;
    bool active_ = false;
    bool capabilities_verified_ = false;
    bool capabilities_request_pending_ = false;
    bool profiles_request_pending_ = false;
    bool profiles_refresh_queued_ = false;
    std::uint64_t manager_generation_ = 0;
};

} // namespace btrfsbackup::kde::monitor
