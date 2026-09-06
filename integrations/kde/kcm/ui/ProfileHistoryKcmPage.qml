// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kcmutils as KCMUtils
import org.kde.ki18n as KI18n

KCMUtils.SimpleKCM {
    id: root

    required property var historyModel

    title: translations.i18n("History")

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ProfileHistory {
        width: parent.width
        historyModel: root.historyModel
    }
}
