// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DesktopLauncher.hpp"

#include <KIO/ApplicationLauncherJob>
#include <KIO/CommandLauncherJob>
#include <KIO/OpenUrlJob>
#include <KJob>
#include <KService>

#include <QObject>

#include <utility>

using Qt::StringLiterals::operator""_s;

namespace btrfsbackup::kde::launcher {

LaunchRequest open_url(QUrl url, QString mime_type) {
    return {
        .method = LaunchMethod::OpenUrl,
        .desktop_name = {},
        .executable = {},
        .arguments = {},
        .urls = {std::move(url)},
        .mime_type = std::move(mime_type),
    };
}

LaunchRequest open_application(QString desktop_name, QList<QUrl> urls) {
    return {
        .method = LaunchMethod::Application,
        .desktop_name = std::move(desktop_name),
        .executable = {},
        .arguments = {},
        .urls = std::move(urls),
        .mime_type = {},
    };
}

LaunchRequest run_command(QString executable, QStringList arguments, QString desktop_name) {
    return {
        .method = LaunchMethod::Command,
        .desktop_name = std::move(desktop_name),
        .executable = std::move(executable),
        .arguments = std::move(arguments),
        .urls = {},
        .mime_type = {},
    };
}

LaunchRequest open_backup_settings(QString profile_id) {
    QStringList arguments{u"kcm_btrfsbackup"_s};
    if (!profile_id.isEmpty())
        arguments << u"--args"_s << std::move(profile_id);
    return run_command(u"systemsettings"_s, std::move(arguments), u"systemsettings"_s);
}

LaunchRequest open_notification_settings() {
    return run_command(u"systemsettings"_s, {u"kcm_notifications"_s}, u"systemsettings"_s);
}

LaunchRequest open_backup_directory(QUrl url) {
    return open_url(std::move(url), u"inode/directory"_s);
}

LaunchRequest open_restore_application(QUrl url) {
    return open_application(u"io.github.btrfsbackup.Restore"_s, {std::move(url)});
}

LaunchRequest open_system_log() {
    return run_command(
        u"konsole"_s,
        {
            u"--hold"_s,
            u"-e"_s,
            u"journalctl"_s,
            u"-u"_s,
            u"btrfs-backupd.service"_s,
            u"--no-pager"_s,
        },
        u"org.kde.konsole"_s
    );
}

LaunchRequest open_partition_manager() {
    return open_application(u"org.kde.partitionmanager"_s);
}

bool application_available(const LaunchRequest& request) {
    return request.method == LaunchMethod::Application &&
        !request.desktop_name.isEmpty() &&
        KService::serviceByDesktopName(request.desktop_name);
}

void launch(const LaunchRequest& request, QObject* parent, FailureHandler failure_handler) {
    KJob* job = nullptr;
    switch (request.method) {
    case LaunchMethod::OpenUrl: {
        if (request.urls.size() != 1) {
            if (failure_handler)
                failure_handler(u"invalid URL launch request"_s);
            return;
        }
        job = new KIO::OpenUrlJob(request.urls.front(), request.mime_type, parent);
        break;
    }
    case LaunchMethod::Application: {
        const KService::Ptr service = KService::serviceByDesktopName(request.desktop_name);
        if (!service) {
            if (failure_handler)
                failure_handler(u"application desktop service was not found"_s);
            return;
        }
        auto* application_job = new KIO::ApplicationLauncherJob(service, parent);
        application_job->setUrls(request.urls);
        job = application_job;
        break;
    }
    case LaunchMethod::Command: {
        auto* command_job = new KIO::CommandLauncherJob(request.executable, request.arguments, parent);
        command_job->setDesktopName(request.desktop_name);
        job = command_job;
        break;
    }
    }

    if (failure_handler) {
        QObject::connect(job, &KJob::result, job, [handler = std::move(failure_handler)](KJob* completed) {
            if (completed->error() != 0)
                handler(completed->errorText());
        });
    }
    job->start();
}

} // namespace btrfsbackup::kde::launcher
