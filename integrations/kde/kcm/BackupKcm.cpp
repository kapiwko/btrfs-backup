// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupKcm.hpp"

#include "DesktopLauncher.hpp"

#include <KPluginFactory>

#include <QDesktopServices>
#include <QGuiApplication>
#include <QClipboard>
#include <QUrl>

namespace btrfsbackup::kde::kcm {

BackupKcm::BackupKcm(QObject* parent, const KPluginMetaData& metadata)
    : KQuickConfigModule(parent, metadata) {
    setButtons(NoAdditionalButton);
}

void BackupKcm::openSystemLog() {
    launcher::launch(launcher::open_system_log(), this);
}

void BackupKcm::openSupportPage() {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/kapiwko/btrfs-backup/issues")));
}

void BackupKcm::openPartitionManager() {
    const auto request = launcher::open_partition_manager();
    if (launcher::application_available(request)) {
        launcher::launch(request, this);
        return;
    }
    QDesktopServices::openUrl(QUrl(QStringLiteral("appstream://org.kde.partitionmanager.desktop")));
}

void BackupKcm::openRecoveryGuide() {
    QDesktopServices::openUrl(QUrl(QStringLiteral(
        "https://github.com/kapiwko/btrfs-backup/blob/master/docs/recovery.md"
    )));
}

void BackupKcm::copyText(const QString& text) {
    QGuiApplication::clipboard()->setText(text);
}

bool BackupKcm::partitionManagerAvailable() const {
    return launcher::application_available(launcher::open_partition_manager());
}

QString BackupKcm::toLocalFile(const QUrl& url) const {
    return url.toLocalFile();
}

ProfileConfigurationModel* BackupKcm::profileConfiguration() {
    return &profile_configuration_;
}

BackupHistoryModel* BackupKcm::profileHistory() {
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
