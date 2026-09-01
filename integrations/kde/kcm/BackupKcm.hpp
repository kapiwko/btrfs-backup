// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <QUrl>
#include "ProfileConfigurationModel.hpp"
#include "ProfileHistoryModel.hpp"
#include "TargetCredentialModel.hpp"
#include "DeviceProvisioningModel.hpp"

namespace btrfsbackup::kde::kcm {

class BackupKcm final : public KQuickConfigModule {
    Q_OBJECT
    Q_PROPERTY(ProfileConfigurationModel* profileConfiguration READ profileConfiguration CONSTANT)
    Q_PROPERTY(ProfileHistoryModel* profileHistory READ profileHistory CONSTANT)
    Q_PROPERTY(TargetCredentialModel* targetCredentials READ targetCredentials CONSTANT)
    Q_PROPERTY(DeviceProvisioningModel* deviceProvisioning READ deviceProvisioning CONSTANT)

  public:
    BackupKcm(QObject* parent, const KPluginMetaData& metadata);

    Q_INVOKABLE void openSystemLog();
    Q_INVOKABLE void openSupportPage();
    Q_INVOKABLE QString toLocalFile(const QUrl& url) const;
    ProfileConfigurationModel* profileConfiguration();
    ProfileHistoryModel* profileHistory();
    TargetCredentialModel* targetCredentials();
    DeviceProvisioningModel* deviceProvisioning();

  private:
    ProfileConfigurationModel profile_configuration_;
    ProfileHistoryModel profile_history_;
    TargetCredentialModel target_credentials_;
    DeviceProvisioningModel device_provisioning_;
};

} // namespace btrfsbackup::kde::kcm
