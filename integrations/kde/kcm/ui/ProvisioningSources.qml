// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property var workflow
    required property var translations
    property var sources: []
    readonly property var selectedSources: sources
    readonly property var availableCandidates: (workflow.provisioning.sourceCandidates ?? [])
        .filter(candidate => !sources.some(source => source.candidateId === candidate.id))

    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing

    RowLayout {
        Layout.fillWidth: true

        QQC2.Label {
            Layout.fillWidth: true
            text: root.translations.i18n("Backup sources")
            font.bold: true
        }
        QQC2.ToolButton {
            id: addSourceButton
            objectName: "addProvisioningSourceButton"
            icon.name: "list-add-symbolic"
            text: root.translations.i18n("Add source")
            enabled: root.availableCandidates.length > 0
            display: QQC2.AbstractButton.TextBesideIcon
            onClicked: sourceDialog.openForAdd()
        }
    }

    Repeater {
        model: root.sources

        delegate: QQC2.ItemDelegate {
            id: sourceRow

            required property var modelData
            required property int index

            objectName: "provisioningSourceRow"
            Layout.fillWidth: true
            hoverEnabled: false

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "folder-sync"
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: implicitWidth
                }
                Kirigami.TitleSubtitle {
                    Layout.fillWidth: true
                    title: sourceRow.modelData.name
                    subtitle: root.sourceSubtitle(sourceRow.modelData)
                    elide: Text.ElideRight
                }
                QQC2.ToolButton {
                    objectName: "editProvisioningSourceButton"
                    icon.name: "edit-entry-symbolic"
                    text: root.translations.i18n("Edit source")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: text
                    onClicked: sourceDialog.openForEdit(sourceRow.index, sourceRow.modelData)
                }
                QQC2.ToolButton {
                    objectName: "removeProvisioningSourceButton"
                    icon.name: "edit-delete-remove-symbolic"
                    text: root.translations.i18n("Remove source")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: text
                    onClicked: root.removeSource(sourceRow.index)
                }
            }
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        visible: root.sources.length === 0
        text: root.translations.i18n("Add at least one Btrfs subvolume to this profile.")
        wrapMode: Text.Wrap
        opacity: 0.75
    }

    SourceDialog {
        id: sourceDialog

        objectName: "provisioningSourceDialog"
        candidateOnly: true
        sourceCandidates: root.availableCandidates

        onAddAccepted: (name, subvolume, localRetention, targetRetention, candidateId) => {
            root.addSource(name, subvolume, localRetention, targetRetention, candidateId)
        }
        onEditAccepted: (index, name, localRetention, targetRetention) => {
            const edited = root.sources.slice()
            edited[index] = Object.assign({}, edited[index], {
                name: name.trim(),
                localRetention: localRetention,
                remoteRetention: targetRetention
            })
            root.sources = edited
        }
    }

    Connections {
        target: root.workflow.provisioning
        function onSourceCandidatesChanged() { root.selectDefaultSource() }
    }

    Component.onCompleted: selectDefaultSource()

    function selectDefaultSource() {
        if (sources.length > 0 || (workflow.provisioning.sourceCandidates?.length ?? 0) === 0)
            return
        const candidate = workflow.provisioning.sourceCandidates[0]
        sources = [{
            candidateId: candidate.id,
            name: sourceName(candidate),
            subvolume: candidate.path,
            localRetention: 30,
            remoteRetention: 30
        }]
    }

    function sourceName(candidate) {
        if (candidate.path === "/home")
            return translations.i18n("Home folder")
        if (candidate.path === "/")
            return translations.i18n("System root")
        const parts = String(candidate.path ?? "").split("/").filter(part => part.length > 0)
        return parts.length > 0 ? parts[parts.length - 1] : translations.i18n("Backup source")
    }

    function sourceSubtitle(source) {
        return translations.i18n("%1 · %2 local / %3 target",
            source.subvolume, source.localRetention, source.remoteRetention)
    }

    function addSource(name, subvolume, localRetention, targetRetention, candidateId) {
        const added = sources.slice()
        added.push({
            candidateId: candidateId,
            name: name.trim(),
            subvolume: subvolume,
            localRetention: localRetention,
            remoteRetention: targetRetention
        })
        sources = added
    }

    function removeSource(index) {
        const remaining = sources.slice()
        remaining.splice(index, 1)
        sources = remaining
    }
}
