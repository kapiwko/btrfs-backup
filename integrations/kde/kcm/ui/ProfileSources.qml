// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property var editor

    signal addRequested()
    signal editRequested(int index, var source)
    signal removeRequested(int index, var source)

    spacing: Kirigami.Units.largeSpacing

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            text: translations.i18n("Sources")
            level: 3
        }
        Kirigami.Separator { Layout.fillWidth: true }
        QQC2.ToolButton {
            icon.name: "list-add-symbolic"
            text: translations.i18n("Add source")
            display: QQC2.AbstractButton.IconOnly
            enabled: root.editor !== null && !root.editor.busy
            onClicked: root.addRequested()
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
        }
    }

    QQC2.Frame {
        Layout.fillWidth: true
        padding: 0

        ColumnLayout {
            width: parent.width
            spacing: 0

            Repeater {
                model: root.editor?.sources ?? []

                delegate: QQC2.ItemDelegate {
                    id: sourceRow
                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    hoverEnabled: false

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: sourceRow.modelData.enabled === false
                                ? "media-playback-pause-symbolic"
                                : "folder-symbolic"
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: implicitWidth
                        }
                        Kirigami.TitleSubtitle {
                            Layout.fillWidth: true
                            title: sourceRow.modelData.name || sourceRow.modelData.id
                            subtitle: sourceRow.modelData.subvolume
                                || translations.i18n("Unknown subvolume")
                            selected: false
                        }
                        QQC2.Label {
                            text: translations.i18n("%1 local / %2 target",
                                sourceRow.modelData.localRetention,
                                sourceRow.modelData.remoteRetention)
                            opacity: 0.7
                        }
                        QQC2.ToolButton {
                            icon.name: "document-edit-symbolic"
                            text: translations.i18n("Edit source")
                            display: QQC2.AbstractButton.IconOnly
                            enabled: root.editor !== null && !root.editor.busy
                            onClicked: root.editRequested(sourceRow.index, sourceRow.modelData)
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                        }
                        QQC2.ToolButton {
                            icon.name: "edit-delete-symbolic"
                            text: translations.i18n("Remove source")
                            display: QQC2.AbstractButton.IconOnly
                            enabled: root.editor !== null && !root.editor.busy
                                && root.editor.sources.length > 1
                            onClicked: root.removeRequested(sourceRow.index, sourceRow.modelData)
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                        }
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                visible: (root.editor?.sources?.length ?? 0) === 0
                text: translations.i18n("No sources configured")
                opacity: 0.7
            }
        }
    }
}
