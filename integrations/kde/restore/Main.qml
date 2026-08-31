// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    required property var controller
    property bool destinationDialogEnabled: true
    width: 620
    height: 430
    minimumWidth: 420
    minimumHeight: 360
    title: translations.i18n("Restore %1", root.controller.sourceName)
    visible: true

    KI18n.KI18nContext {
        id: translations
        translationDomain: "btrfs-backup-kde-restore"
    }

    pageStack.initialPage: RestorePage {
        controller: root.controller
        destinationDialogEnabled: root.destinationDialogEnabled
    }
}
