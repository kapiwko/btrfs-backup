// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <QUrl>
#include <BackupHistoryModel.hpp>
#include <BackupReminderSettings.hpp>
#include "ProfileConfigurationModel.hpp"
#include "TargetCredentialModel.hpp"
#include "DeviceProvisioningModel.hpp"

namespace btrfsbackup::kde::kcm {

class BackupKcm final : public KQuickConfigModule {
    Q_OBJECT
    Q_PROPERTY(ProfileConfigurationModel* profileConfiguration READ profileConfiguration CONSTANT)
    Q_PROPERTY(BackupHistoryModel* profileHistory READ profileHistory CONSTANT)
    Q_PROPERTY(TargetCredentialModel* targetCredentials READ targetCredentials CONSTANT)
    Q_PROPERTY(DeviceProvisioningModel* deviceProvisioning READ deviceProvisioning CONSTANT)
    Q_PROPERTY(BackupReminderSettings* backupReminderSettings READ backupReminderSettings CONSTANT)
    Q_PROPERTY(bool partitionManagerAvailable READ partitionManagerAvailable CONSTANT)

  public:
    BackupKcm(QObject* parent, const KPluginMetaData& metadata);

    Q_INVOKABLE void openSystemLog();
    Q_INVOKABLE void openSupportPage();
    Q_INVOKABLE void openPartitionManager();
    Q_INVOKABLE void openRecoveryGuide();
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE QString toLocalFile(const QUrl& url) const;
    [[nodiscard]] bool partitionManagerAvailable() const;
    ProfileConfigurationModel* profileConfiguration();
    BackupHistoryModel* profileHistory();
    TargetCredentialModel* targetCredentials();
    DeviceProvisioningModel* deviceProvisioning();
    BackupReminderSettings* backupReminderSettings();

  private:
    ProfileConfigurationModel profile_configuration_;
    BackupHistoryModel profile_history_;
    TargetCredentialModel target_credentials_;
    DeviceProvisioningModel device_provisioning_;
    BackupReminderSettings backup_reminder_settings_;
};

} // namespace btrfsbackup::kde::kcm
