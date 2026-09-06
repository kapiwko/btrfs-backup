// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

class QObject;

namespace btrfsbackup::kde::launcher {

enum class LaunchMethod {
    OpenUrl,
    Application,
    Command,
};

struct LaunchRequest {
    LaunchMethod method = LaunchMethod::OpenUrl;
    QString desktop_name;
    QString executable;
    QStringList arguments;
    QList<QUrl> urls;
    QString mime_type;
};

using FailureHandler = std::function<void(const QString&)>;

LaunchRequest open_url(QUrl url, QString mime_type = {});
LaunchRequest open_application(QString desktop_name, QList<QUrl> urls = {});
LaunchRequest run_command(QString executable, QStringList arguments, QString desktop_name);

LaunchRequest open_backup_settings(QString profile_id = {});
LaunchRequest open_notification_settings();
LaunchRequest open_backup_directory(QUrl url);
LaunchRequest open_restore_application(QUrl url);
LaunchRequest open_partition_manager();

bool application_available(const LaunchRequest& request);
void launch(const LaunchRequest& request, QObject* parent = nullptr, FailureHandler failure_handler = {});

} // namespace btrfsbackup::kde::launcher
