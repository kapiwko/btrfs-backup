// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KAbstractFileItemActionPlugin>

class PreviousVersionsAction final : public KAbstractFileItemActionPlugin {
    Q_OBJECT

  public:
    explicit PreviousVersionsAction(QObject* parent);
    QList<QAction*> actions(const KFileItemListProperties& properties, QWidget* parent_widget) override;

  private:
    void resolve_and_open(const QString& local_path, QWidget* parent_widget);
};
