// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupProgressMonitor.hpp"

#include "BackupProgressJob.hpp"
#include "ManagerApi.hpp"

#include <KLocalizedString>
#include <KUiServerV2JobTracker>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

#include <algorithm>

namespace {

constexpr int active_poll_interval_ms = 1000;
constexpr int idle_poll_interval_ms = 5000;
constexpr int profile_refresh_interval_ms = 60000;

QString activity_text(const QString& activity, const QString& phase) {
    if (activity == QStringLiteral("sizing")) {
        return i18n("Calculating transfer size");
    }
    if (activity == QStringLiteral("transferring")) {
        return i18n("Transferring backup data");
    }
    if (phase == QStringLiteral("recover-pending")) {
        return i18n("Recovering interrupted backup");
    }
    if (phase == QStringLiteral("create-snapshot")) {
        return i18n("Creating local snapshot");
    }
    if (phase == QStringLiteral("verify-received")) {
        return i18n("Verifying received snapshot");
    }
    if (phase == QStringLiteral("commit-received")) {
        return i18n("Committing received snapshot");
    }
    if (phase == QStringLiteral("validating-target")) {
        return i18n("Validating backup target");
    }
    return i18n("Preparing backup");
}

} // namespace

BackupProgressMonitor::BackupProgressMonitor(
    QDBusConnection bus,
    KUiServerV2JobTracker& tracker,
    QObject* parent
)
    : QObject(parent),
      bus_(std::move(bus)),
      service_watcher_(
          QLatin1String(btrfsbackup::kde::manager_service),
          bus_,
          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
          this
      ),
      tracker_(tracker) {
    poll_timer_.setInterval(idle_poll_interval_ms);
    profile_refresh_timer_.setInterval(profile_refresh_interval_ms);
    connect(&poll_timer_, &QTimer::timeout, this, &BackupProgressMonitor::refresh);
    connect(
        &profile_refresh_timer_,
        &QTimer::timeout,
        this,
        &BackupProgressMonitor::request_profiles
    );
    connect(&service_watcher_, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        if (active_) {
            connect_to_manager();
        }
    });
    connect(&service_watcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        if (active_) {
            manager_unavailable();
        }
    });
}

void BackupProgressMonitor::start() {
    if (active_) {
        return;
    }
    active_ = true;
    connect_to_manager();
}

void BackupProgressMonitor::connect_to_manager() {
    poll_timer_.stop();
    capabilities_verified_ = false;
    if (capabilities_request_pending_) {
        return;
    }
    capabilities_request_pending_ = true;
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::manager_call(bus_, QStringLiteral("GetCapabilities")),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        capabilities_request_pending_ = false;
        if (!active_) {
            return;
        }
        if (reply.isError()) {
            manager_unavailable();
            return;
        }

        const auto capabilities = btrfsbackup::kde::parse_capabilities(reply.value());
        if (!capabilities.has_value() || capabilities->api_major != 1 ||
            capabilities->public_status_schema_version != 3) {
            qWarning() << "btrfs-backup KDE monitor received incompatible manager capabilities";
            manager_unavailable();
            return;
        }

        capabilities_verified_ = true;
        request_profiles();
        poll_timer_.start();
        profile_refresh_timer_.start();
    });
}

void BackupProgressMonitor::manager_unavailable() {
    poll_timer_.stop();
    profile_refresh_timer_.stop();
    capabilities_verified_ = false;
    profiles_request_pending_ = false;
    pending_status_requests_.clear();
    for (auto job : std::as_const(jobs_)) {
        if (job) {
            job->finish_with_error(i18n("Backup service unavailable"));
        }
    }
    jobs_.clear();
    suppressed_runs_.clear();
}

void BackupProgressMonitor::request_profiles() {
    if (!capabilities_verified_ || profiles_request_pending_) {
        return;
    }
    profiles_request_pending_ = true;
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::manager_call(bus_, QStringLiteral("ListProfiles")),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        profiles_request_pending_ = false;
        if (!active_ || !capabilities_verified_) {
            return;
        }
        if (reply.isError()) {
            qWarning() << "btrfs-backup KDE monitor could not list profiles:" << reply.error().message();
            return;
        }
        apply_profiles(reply.value());
    });
}

void BackupProgressMonitor::request_status(const Profile& profile) {
    if (pending_status_requests_.contains(profile.id)) {
        return;
    }
    pending_status_requests_.insert(profile.id);
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::manager_call(bus_, QStringLiteral("GetStatus"), {profile.id}),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, profile](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        pending_status_requests_.remove(profile.id);
        if (!active_ || !capabilities_verified_ || !profiles_.contains(profile.id)) {
            return;
        }
        if (reply.isError()) {
            qWarning() << "btrfs-backup KDE monitor could not read profile status:"
                       << profile.id << reply.error().message();
            return;
        }
        apply_status(profile, reply.value());
    });
}

void BackupProgressMonitor::request_cancel(const QString& profile_id, const QString& run_id) {
    const QString key = suppression_key(profile_id, run_id);
    suppressed_runs_.insert(key);
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::manager_call(
            bus_,
            QStringLiteral("CancelBackup"),
            {profile_id, run_id}
        ),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, key](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            suppressed_runs_.remove(key);
            qWarning() << "btrfs-backup KDE monitor could not cancel the backup:"
                       << reply.error().message();
        }
    });
}

void BackupProgressMonitor::refresh() {
    if (!capabilities_verified_) {
        return;
    }
    for (const Profile& profile : std::as_const(profiles_)) {
        request_status(profile);
    }
}

void BackupProgressMonitor::apply_profiles(const QString& payload) {
    const auto decoded_profiles = btrfsbackup::kde::parse_profiles(payload);
    if (!decoded_profiles.has_value()) {
        qWarning() << "btrfs-backup KDE monitor received an invalid profile list";
        return;
    }

    QHash<QString, Profile> profiles;
    for (Profile profile : *decoded_profiles) {
        profiles.insert(profile.id, std::move(profile));
    }

    for (auto iterator = jobs_.begin(); iterator != jobs_.end();) {
        if (!profiles.contains(iterator.key())) {
            if (iterator.value()) {
                iterator.value()->finish_with_error(i18n("Backup profile is no longer available"));
            }
            iterator = jobs_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    profiles_ = std::move(profiles);
    for (const Profile& profile : std::as_const(profiles_)) {
        request_status(profile);
    }
}

void BackupProgressMonitor::apply_status(const Profile& profile, const QString& payload) {
    const auto decoded_status = btrfsbackup::kde::parse_status(payload);
    if (!decoded_status.has_value()) {
        qWarning() << "btrfs-backup KDE monitor received an invalid status for" << profile.id;
        return;
    }
    const Status& status = *decoded_status;

    if (!btrfsbackup::kde::active_run_state(status.state) || status.run_id.isEmpty()) {
        finish_job(profile.id, status);
        return;
    }
    if (suppressed_runs_.contains(suppression_key(profile.id, status.run_id))) {
        return;
    }

    const QPointer<BackupProgressJob> current = jobs_.value(profile.id);
    if (!current || current->run_id() != status.run_id) {
        if (current) {
            current->finish_with_error(i18n("A newer backup run replaced this progress report"));
        }
        create_job(profile, status);
    }

    const QPointer<BackupProgressJob> job = jobs_.value(profile.id);
    if (job) {
        job->update(
            status.overall_progress,
            status.speed_bps,
            status.can_cancel,
            activity_text(status.activity, status.phase),
            status.source_name,
            status.target_name.isEmpty() ? profile.target_name : status.target_name
        );
    }
}

void BackupProgressMonitor::create_job(const Profile& profile, const Status& status) {
    auto* job = new BackupProgressJob(
        profile.id,
        status.run_id,
        profile.name.isEmpty() ? profile.id : profile.name,
        [this](const QString& profile_id, const QString& run_id) {
            request_cancel(profile_id, run_id);
        },
        this
    );
    jobs_.insert(profile.id, job);
    connect(job, &QObject::destroyed, this, [this, profile_id = profile.id, job]() {
        if (jobs_.value(profile_id) == job) {
            jobs_.remove(profile_id);
            update_poll_interval();
        }
    });
    tracker_.registerJob(job);
    job->start();
    update_poll_interval();
}

void BackupProgressMonitor::finish_job(const QString& profile_id, const Status& status) {
    for (auto iterator = suppressed_runs_.begin(); iterator != suppressed_runs_.end();) {
        if (iterator->startsWith(profile_id + QLatin1Char('\n'))) {
            iterator = suppressed_runs_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    const QPointer<BackupProgressJob> job = jobs_.take(profile_id);
    if (!job) {
        return;
    }
    if (status.state == QStringLiteral("failed")) {
        job->finish_with_error(
            status.error_code.isEmpty() ? i18n("Backup failed") : status.error_code
        );
    } else if (status.state == QStringLiteral("cancelled")) {
        job->finish_cancelled();
    } else {
        job->finish_successfully();
    }
    update_poll_interval();
}

void BackupProgressMonitor::update_poll_interval() {
    poll_timer_.setInterval(
        jobs_.isEmpty() ? idle_poll_interval_ms : active_poll_interval_ms
    );
}

QString BackupProgressMonitor::suppression_key(
    const QString& profile_id,
    const QString& run_id
) const {
    return profile_id + QLatin1Char('\n') + run_id;
}
