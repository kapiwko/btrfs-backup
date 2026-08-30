// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include "ProfileConfigurationModel.hpp"

namespace btrfsbackup::kde::kcm {

class BackupKcm final : public KQuickConfigModule {
    Q_OBJECT
    Q_PROPERTY(ProfileConfigurationModel* profileConfiguration READ profileConfiguration CONSTANT)

  public:
    BackupKcm(QObject* parent, const KPluginMetaData& metadata);

    Q_INVOKABLE void openSystemLog();
    Q_INVOKABLE void openSupportPage();
    ProfileConfigurationModel* profileConfiguration();

  private:
    ProfileConfigurationModel profile_configuration_;
};

} // namespace btrfsbackup::kde::kcm
