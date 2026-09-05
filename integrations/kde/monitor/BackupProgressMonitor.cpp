// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupProgressMonitor.hpp"

#include "BackupProgressJob.hpp"
#include "BackupReminderPolicy.hpp"
#include "ManagerApi.hpp"
#include "TargetStorageNotificationPolicy.hpp"

#include <KLocalizedString>
#include <KSharedConfig>
#include <KUiServerV2JobTracker>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

#include <algorithm>
#include <utility>

namespace btrfsbackup::kde::monitor {

namespace {

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
      manager_events_(bus_, this),
      service_watcher_(
          QLatin1String(btrfsbackup::manager_protocol::service_name),
          bus_,
          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
          this
      ),
      cancellation_dispatcher_(bus_, this),
      terminal_notifications_(),
      tracker_(tracker) {
    reminder_config_watcher_ = KConfigWatcher::create(
        KSharedConfig::openConfig(BackupReminderSettings::configFileName())
    );
    connect(
        reminder_config_watcher_.get(),
        &KConfigWatcher::configChanged,
        this,
        [this]() { evaluate_reminders(); }
    );
    reminder_timer_.setInterval(15 * 60 * 1000);
    reminder_timer_.setSingleShot(false);
    connect(&reminder_timer_, &QTimer::timeout, this, &BackupProgressMonitor::evaluate_reminders);
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
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::profilesChanged, this, [this]() {
        if (active_ && capabilities_verified_)
            request_profiles();
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::statusChanged, this, [this](const QString& profile_id) {
        if (!active_ || !capabilities_verified_)
            return;
        const auto profile = profiles_.find(profile_id);
        if (profile == profiles_.end()) {
            request_profiles();
            return;
        }
        request_status(profile.value());
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::deviceStateChanged, this, [this](const QString& profile_id) {
        if (!active_ || !capabilities_verified_)
            return;
        const auto profile = profiles_.find(profile_id);
        if (profile == profiles_.end()) {
            request_profiles();
            return;
        }
        request_target_status(profile.value());
    });
    connect(
        &cancellation_dispatcher_,
        &CancellationRequestDispatcher::rejected,
        this,
        [this](const QString& profile_id, const QString& run_id, const QString& reason) {
            const QPointer<BackupProgressJob> job = jobs_.value(profile_id);
            if (job && job->run_id() == run_id) {
                job->cancellation_rejected();
            }
            qWarning() << "btrfs-backup KDE monitor could not cancel the backup:" << reason;
        }
    );
}

void BackupProgressMonitor::start() {
    if (active_) {
        return;
    }
    active_ = true;
    reminder_timer_.start();
    connect_to_manager();
}

void BackupProgressMonitor::connect_to_manager() {
    capabilities_verified_ = false;
    if (capabilities_request_pending_) {
        return;
    }
    const std::uint64_t generation = ++manager_generation_;
    capabilities_request_pending_ = true;
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::ManagerClient{bus_}.capabilities(),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (generation != manager_generation_) {
            return;
        }
        capabilities_request_pending_ = false;
        if (!active_) {
            return;
        }
        if (reply.isError()) {
            manager_unavailable();
            return;
        }

        const auto capabilities = btrfsbackup::kde::parse_capabilities(reply.value());
        if (!capabilities.has_value() ||
            capabilities->api_major != btrfsbackup::manager_protocol::api_major ||
            capabilities->public_status_schema_version != btrfsbackup::manager_protocol::public_status_schema_version ||
            !capabilities->features.contains(QLatin1String(btrfsbackup::manager_protocol::feature::change_signals))) {
            qWarning() << "btrfs-backup KDE monitor received incompatible manager capabilities";
            manager_unavailable();
            return;
        }

        manager_features_ = capabilities->features;
        capabilities_verified_ = true;
        request_profiles();
    });
}

void BackupProgressMonitor::manager_unavailable() {
    ++manager_generation_;
    capabilities_verified_ = false;
    capabilities_request_pending_ = false;
    manager_features_.clear();
    profiles_request_pending_ = false;
    profiles_refresh_queued_ = false;
    pending_status_requests_.clear();
    queued_status_requests_.clear();
    pending_target_requests_.clear();
    queued_target_requests_.clear();
    for (auto job : std::as_const(jobs_)) {
        if (job) {
            job->stop_tracking();
        }
    }
    jobs_.clear();
}

void BackupProgressMonitor::request_profiles() {
    if (!capabilities_verified_) {
        return;
    }
    if (profiles_request_pending_) {
        profiles_refresh_queued_ = true;
        return;
    }
    const std::uint64_t generation = manager_generation_;
    profiles_request_pending_ = true;
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::ManagerClient{bus_}.profiles(),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (generation != manager_generation_) {
            return;
        }
        profiles_request_pending_ = false;
        if (!active_ || !capabilities_verified_) {
            return;
        }
        if (reply.isError()) {
            qWarning() << "btrfs-backup KDE monitor could not list profiles:" << reply.error().message();
        } else {
            apply_profiles(reply.value());
        }
        if (std::exchange(profiles_refresh_queued_, false))
            request_profiles();
    });
}

void BackupProgressMonitor::request_status(const Profile& profile) {
    if (pending_status_requests_.contains(profile.id)) {
        queued_status_requests_.insert(profile.id);
        return;
    }
    const std::uint64_t generation = manager_generation_;
    pending_status_requests_.insert(profile.id);
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::ManagerClient{bus_}.status(profile.id),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, profile, generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (generation != manager_generation_) {
            return;
        }
        pending_status_requests_.remove(profile.id);
        if (!active_ || !capabilities_verified_ || !profiles_.contains(profile.id)) {
            return;
        }
        if (reply.isError()) {
            qWarning() << "btrfs-backup KDE monitor could not read profile status:"
                       << profile.id << reply.error().message();
        } else {
            apply_status(profile, reply.value());
        }
        if (queued_status_requests_.remove(profile.id) > 0 && profiles_.contains(profile.id))
            request_status(profiles_.value(profile.id));
    });
}

void BackupProgressMonitor::request_target_status(const Profile& profile) {
    if (!manager_features_.contains(QLatin1String(btrfsbackup::manager_protocol::feature::device_state)) ||
        !manager_features_.contains(QLatin1String(btrfsbackup::manager_protocol::feature::target_storage_usage))) {
        return;
    }
    if (pending_target_requests_.contains(profile.id)) {
        queued_target_requests_.insert(profile.id);
        return;
    }
    const std::uint64_t generation = manager_generation_;
    pending_target_requests_.insert(profile.id);
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::ManagerClient{bus_}.deviceState(profile.id),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, profile, generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (generation != manager_generation_)
            return;
        pending_target_requests_.remove(profile.id);
        if (!active_ || !capabilities_verified_ || !profiles_.contains(profile.id))
            return;
        if (reply.isError()) {
            qWarning() << "btrfs-backup KDE monitor could not read target storage:"
                       << profile.id << reply.error().message();
        } else {
            apply_target_status(profile, reply.value());
        }
        if (queued_target_requests_.remove(profile.id) > 0 && profiles_.contains(profile.id))
            request_target_status(profiles_.value(profile.id));
    });
}

void BackupProgressMonitor::request_cancel(const QString& profile_id, const QString& run_id) {
    cancellation_dispatcher_.request(profile_id, run_id);
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
                iterator.value()->stop_tracking();
            }
            iterator = jobs_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    profiles_ = std::move(profiles);
    for (auto iterator = statuses_.begin(); iterator != statuses_.end();) {
        iterator = profiles_.contains(iterator.key()) ? std::next(iterator) : statuses_.erase(iterator);
    }
    for (auto iterator = target_statuses_.begin(); iterator != target_statuses_.end();) {
        iterator = profiles_.contains(iterator.key()) ? std::next(iterator) : target_statuses_.erase(iterator);
    }
    for (const Profile& profile : std::as_const(profiles_)) {
        request_status(profile);
        request_target_status(profile);
    }
}

void BackupProgressMonitor::apply_target_status(const Profile& profile, const QString& payload) {
    const auto decoded_target = btrfsbackup::kde::parse_target_status(payload);
    if (!decoded_target.has_value() || decoded_target->profile_id != profile.id) {
        qWarning() << "btrfs-backup KDE monitor received an invalid target status for" << profile.id;
        return;
    }
    target_statuses_.insert(profile.id, *decoded_target);
    evaluate_target_storage(profile, *decoded_target, BackupReminderSettings::load());
}

void BackupProgressMonitor::apply_status(const Profile& profile, const QString& payload) {
    const auto decoded_status = btrfsbackup::kde::parse_status(payload);
    if (!decoded_status.has_value()) {
        qWarning() << "btrfs-backup KDE monitor received an invalid status for" << profile.id;
        return;
    }
    const Status& status = *decoded_status;
    statuses_.insert(profile.id, status);
    evaluate_reminder(profile, status, BackupReminderSettings::load());

    if (!btrfsbackup::kde::active_run_state(status.state) || status.run_id.isEmpty()) {
        finish_job(profile.id, status);
        return;
    }
    const QPointer<BackupProgressJob> current = jobs_.value(profile.id);
    if (!current || current->run_id() != status.run_id) {
        if (current) {
            current->stop_tracking();
        }
        create_job(profile, status);
    }

    const QPointer<BackupProgressJob> job = jobs_.value(profile.id);
    if (job) {
        job->update(
            status.overall_progress,
            status.speed_bps,
            status.can_cancel && manager_features_.contains(QLatin1String(btrfsbackup::manager_protocol::feature::cancel_backup)),
            activity_text(status.activity, status.phase),
            status.source_name,
            status.target_name.isEmpty() ? profile.target_name : status.target_name
        );
    }
}

void BackupProgressMonitor::evaluate_reminders() {
    const BackupReminderConfiguration configuration = BackupReminderSettings::load();
    for (auto iterator = statuses_.cbegin(); iterator != statuses_.cend(); ++iterator) {
        const auto profile = profiles_.constFind(iterator.key());
        if (profile != profiles_.cend())
            evaluate_reminder(profile.value(), iterator.value(), configuration);
    }
    for (auto iterator = target_statuses_.cbegin(); iterator != target_statuses_.cend(); ++iterator) {
        const auto profile = profiles_.constFind(iterator.key());
        if (profile != profiles_.cend())
            evaluate_target_storage(profile.value(), iterator.value(), configuration);
    }
}

void BackupProgressMonitor::evaluate_target_storage(
    const Profile& profile,
    const Target& target,
    const BackupReminderConfiguration& configuration
) {
    terminal_notifications_.publish_target_storage(
        profile.id,
        profile.name,
        target.target_name.isEmpty() ? profile.target_name : target.target_name,
        monitor::evaluate_target_storage(profile, target, configuration)
    );
}

void BackupProgressMonitor::evaluate_reminder(
    const Profile& profile,
    const Status& status,
    const BackupReminderConfiguration& configuration
) {
    terminal_notifications_.publish_backup_reminder(
        profile.id,
        profile.name,
        status.target_name.isEmpty() ? profile.target_name : status.target_name,
        evaluate_backup_reminder(profile, status, configuration)
    );
}

void BackupProgressMonitor::create_job(const Profile& profile, const Status& status) {
    auto* job = new BackupProgressJob(
        profile.id,
        status.run_id,
        profile.name.isEmpty() ? profile.id : profile.name,
        status.operation_kind,
        [this](const QString& profile_id, const QString& run_id) {
            request_cancel(profile_id, run_id);
        },
        this
    );
    jobs_.insert(profile.id, job);
    connect(job, &QObject::destroyed, this, [this, profile_id = profile.id, job]() {
        if (jobs_.value(profile_id) == job)
            jobs_.remove(profile_id);
    });
    tracker_.registerJob(job);
    job->start();
}

void BackupProgressMonitor::finish_job(const QString& profile_id, const Status& status) {
    const auto profile = profiles_.constFind(profile_id);
    terminal_notifications_.publish(
        profile_id,
        status.run_id,
        profile == profiles_.cend() ? profile_id : profile->name,
        status.operation_kind,
        status.state,
        status.error_code
    );

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
}

} // namespace btrfsbackup::kde::monitor
