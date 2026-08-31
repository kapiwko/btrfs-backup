// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Item {
    id: root

    required property var editor

    signal addRequested(string name, string subvolume, int localRetention, int targetRetention)
    signal editRequested(int index, string name, int localRetention, int targetRetention)
    signal removeRequested(int index, var source)

    implicitHeight: Math.ceil(sourceList.count > 0 ? sourceList.contentHeight : (sourceList.headerItem?.implicitHeight ?? 0) + sourceList.emptyContentHeight)
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ListView {
        id: sourceList

        readonly property real emptyContentHeight: Kirigami.Units.gridUnit * 7

        anchors.fill: parent
        model: root.editor?.sources ?? []
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        clip: false

        header: Kirigami.InlineViewHeader {
            width: sourceList.width
            text: translations.i18n("Sources")
            actions: [
                Kirigami.Action {
                    objectName: "addSourceAction"
                    icon.name: "list-add-symbolic"
                    text: translations.i18n("Add source")
                    enabled: root.editor !== null && !root.editor.busy
                    onTriggered: sourceDialog.openForAdd()
                }
            ]
        }

        delegate: QQC2.ItemDelegate {
            id: sourceRow
            objectName: "sourceRow"

            required property var modelData
            required property int index

            width: ListView.view?.width ?? implicitWidth
            Kirigami.Theme.useAlternateBackgroundColor: true
            onClicked: sourceDialog.openForEdit(sourceRow.index, sourceRow.modelData)

            contentItem: Kirigami.TitleSubtitleWithActions {
                title: sourceRow.modelData.name || sourceRow.modelData.id
                subtitle: root.sourceSubtitle(sourceRow.modelData)
                elide: Text.ElideRight
                selected: sourceRow.pressed || sourceRow.highlighted
                displayHint: QQC2.Button.IconOnly
                actions: [
                    Kirigami.Action {
                        objectName: "editSourceAction"
                        icon.name: "edit-entry-symbolic"
                        text: translations.i18n("Edit source")
                        tooltip: text
                        enabled: root.editor !== null && !root.editor.busy
                        onTriggered: sourceDialog.openForEdit(sourceRow.index, sourceRow.modelData)
                    },
                    Kirigami.Action {
                        icon.name: "edit-delete-remove-symbolic"
                        text: translations.i18n("Remove source")
                        tooltip: text
                        enabled: root.editor !== null && !root.editor.busy && root.editor.sources.length > 1
                        onTriggered: root.removeRequested(sourceRow.index, sourceRow.modelData)
                    }
                ]
            }
        }

        Kirigami.PlaceholderMessage {
            width: parent.width - Kirigami.Units.largeSpacing * 4
            anchors.horizontalCenter: parent.horizontalCenter
            y: (sourceList.headerItem?.height ?? 0) + Kirigami.Units.largeSpacing * 2
            visible: sourceList.count === 0
            icon.name: "folder-symbolic"
            text: translations.i18n("No sources configured")
        }
    }

    SourceDialog {
        id: sourceDialog
        objectName: "sourceDialog"
        sourceCandidates: root.editor?.sourceCandidates ?? []

        onAddAccepted: (name, subvolume, localRetention, targetRetention) => {
            root.addRequested(name, subvolume, localRetention, targetRetention);
        }
        onEditAccepted: (index, name, localRetention, targetRetention) => {
            root.editRequested(index, name, localRetention, targetRetention);
        }
    }

    function sourceSubtitle(source) {
        const subvolume = source.subvolume || translations.i18n("Unknown subvolume");
        const retention = translations.i18n("%1 local / %2 target", source.localRetention, source.remoteRetention);
        return subvolume + " · " + retention;
    }
}
