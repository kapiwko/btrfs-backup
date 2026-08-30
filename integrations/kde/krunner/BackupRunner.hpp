// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KRunner/AbstractRunner>

class BackupRunner final : public KRunner::AbstractRunner {
    Q_OBJECT

  public:
    BackupRunner(QObject* parent, const KPluginMetaData& metadata);
    void match(KRunner::RunnerContext& context) override;
    void run(const KRunner::RunnerContext& context, const KRunner::QueryMatch& match) override;

  private:
    void resolve_versions(const QString& local_path);
};
