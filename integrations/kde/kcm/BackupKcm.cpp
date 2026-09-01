// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupKcm.hpp"

#include <KPluginFactory>

#include <QDesktopServices>
#include <QProcess>
#include <QUrl>

namespace btrfsbackup::kde::kcm {

BackupKcm::BackupKcm(QObject* parent, const KPluginMetaData& metadata)
    : KQuickConfigModule(parent, metadata) {
    setButtons(NoAdditionalButton);
}

void BackupKcm::openSystemLog() {
    QProcess::startDetached(
        QStringLiteral("konsole"),
        {
            QStringLiteral("--hold"),
            QStringLiteral("-e"),
            QStringLiteral("journalctl"),
            QStringLiteral("-u"),
            QStringLiteral("btrfs-backupd.service"),
            QStringLiteral("--no-pager"),
        }
    );
}

void BackupKcm::openSupportPage() {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/kapiwko/btrfs-backup/issues")));
}

QString BackupKcm::toLocalFile(const QUrl& url) const {
    return url.toLocalFile();
}

ProfileConfigurationModel* BackupKcm::profileConfiguration() {
    return &profile_configuration_;
}

ProfileHistoryModel* BackupKcm::profileHistory() {
    return &profile_history_;
}

TargetCredentialModel* BackupKcm::targetCredentials() {
    return &target_credentials_;
}
DeviceProvisioningModel* BackupKcm::deviceProvisioning() {
    return &device_provisioning_;
}

} // namespace btrfsbackup::kde::kcm

K_PLUGIN_CLASS_WITH_JSON(btrfsbackup::kde::kcm::BackupKcm, "kcm_btrfsbackup.json")

#include "BackupKcm.moc"
