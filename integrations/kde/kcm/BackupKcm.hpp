// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>

namespace btrfsbackup::kde::kcm {

class BackupKcm final : public KQuickConfigModule {
    Q_OBJECT

  public:
    BackupKcm(QObject* parent, const KPluginMetaData& metadata);

    Q_INVOKABLE void openSystemLog();
    Q_INVOKABLE void openSupportPage();
};

} // namespace btrfsbackup::kde::kcm
