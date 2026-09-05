// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DesktopLauncher.hpp"

#include <iostream>

using Qt::StringLiterals::operator""_s;

int main() {
    using btrfsbackup::kde::launcher::LaunchMethod;
    using btrfsbackup::kde::launcher::LaunchRequest;
    using btrfsbackup::kde::launcher::open_backup_directory;
    using btrfsbackup::kde::launcher::open_backup_settings;
    using btrfsbackup::kde::launcher::open_partition_manager;
    using btrfsbackup::kde::launcher::open_restore_application;
    using btrfsbackup::kde::launcher::open_system_log;
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    };

    const LaunchRequest settings = open_backup_settings(u"daily"_s);
    expect(settings.method == LaunchMethod::Command, "backup settings must use a command job");
    expect(settings.executable == u"systemsettings"_s, "wrong System Settings executable");
    expect(
        settings.arguments == QStringList{u"kcm_btrfsbackup"_s, u"--args"_s, u"daily"_s},
        "profile argument was not preserved"
    );
    expect(settings.desktop_name == u"systemsettings"_s, "missing System Settings startup identity");

    const QUrl backup_url(u"btrfsbackup:/daily"_s);
    const LaunchRequest directory = open_backup_directory(backup_url);
    expect(directory.method == LaunchMethod::OpenUrl, "backup directory must use OpenUrlJob");
    expect(directory.urls == QList<QUrl>{backup_url}, "backup directory URL changed");
    expect(directory.mime_type == u"inode/directory"_s, "backup directory MIME type missing");

    const LaunchRequest restore = open_restore_application(backup_url);
    expect(restore.method == LaunchMethod::Application, "restore must use ApplicationLauncherJob");
    expect(
        restore.desktop_name == u"io.github.btrfsbackup.Restore"_s,
        "wrong restore desktop service"
    );
    expect(restore.urls == QList<QUrl>{backup_url}, "restore URL changed");

    const LaunchRequest log = open_system_log();
    expect(log.method == LaunchMethod::Command, "system log must use a command job");
    expect(log.executable == u"konsole"_s, "wrong terminal executable");
    expect(log.desktop_name == u"org.kde.konsole"_s, "missing terminal startup identity");
    expect(log.arguments.contains(u"btrfs-backupd.service"_s), "daemon unit missing from log command");

    const LaunchRequest partition_manager = open_partition_manager();
    expect(
        partition_manager.desktop_name == u"org.kde.partitionmanager"_s,
        "wrong partition manager desktop service"
    );

    return failures == 0 ? 0 : 1;
}
