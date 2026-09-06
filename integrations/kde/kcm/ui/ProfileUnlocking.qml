// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.ki18n as KI18n
import org.kde.kirigami as Kirigami

Item {
    id: root
    required property var editor
    required property string profileId
    property var credentialModel: null
    readonly property var currentCredentials: credentialModel?.credentials ?? []
    readonly property int credentialCount: stableCredentials.count

    ListModel {
        id: stableCredentials
    }

    onCurrentCredentialsChanged: synchronizeCredentials()

    implicitHeight: Math.ceil(methodList.count > 0
        ? methodList.contentHeight
        : (methodList.headerItem?.implicitHeight ?? 0) + methodList.emptyContentHeight)
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Rectangle { anchors.fill: parent; color: Kirigami.Theme.backgroundColor }

    KI18n.KI18nContext {
        id: translations
        translationDomain: "kcm_btrfsbackup"
    }

    ListView {
        id: methodList

        readonly property real emptyContentHeight:
            Kirigami.Units.largeSpacing * 4 + emptyMessage.implicitHeight

        anchors.fill: parent
        model: stableCredentials
        interactive: false
        boundsBehavior: Flickable.StopAtBounds

        header: Kirigami.InlineViewHeader {
            width: methodList.width
            text: translations.i18n("Unlocking methods")
            actions: [
                Kirigami.Action {
                    icon.name: "list-add-symbolic"
                    text: translations.i18n("Add unlocking method")
                    enabled: root.credentialModel !== null && !root.credentialModel.busy
                    onTriggered: addMenu.popup()
                }
            ]

            QQC2.Menu {
                id: addMenu
                QQC2.MenuItem {
                    text: translations.i18n("Add passphrase")
                    icon.name: "dialog-password-symbolic"
                    onTriggered: passphraseDialog.open()
                }
                QQC2.MenuItem {
                    text: translations.i18n("Generate key")
                    icon.name: "password-generate-symbolic"
                    onTriggered: generateKeyDialog.open()
                }
                QQC2.MenuItem {
                    text: translations.i18n("Add key file")
                    icon.name: "document-open-symbolic"
                    onTriggered: importKeyDialog.open()
                }
            }
        }

        delegate: Rectangle {
            id: methodRow
            objectName: "unlockingMethodRow"
            required property string credentialId
            required property string label
            required property string credentialType
            required property int keyslot
            required property bool managed
            required property bool automatic
            width: ListView.view?.width ?? implicitWidth
            implicitHeight: methodContent.implicitHeight + Kirigami.Units.smallSpacing * 2
            color: Kirigami.Theme.alternateBackgroundColor

            RowLayout {
                id: methodContent
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: root.methodIcon(methodRow)
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: implicitWidth
                }

                Kirigami.TitleSubtitleWithActions {
                    id: methodDetails
                    objectName: "unlockingMethodDetails"
                    Layout.fillWidth: true
                    title: methodRow.managed && methodRow.label
                        ? methodRow.label
                        : translations.i18n("LUKS key slot %1", methodRow.keyslot)
                    subtitle: root.methodDescription(methodRow)
                    selected: false
                    actions: [
                        Kirigami.Action {
                            icon.name: "edit-delete-symbolic"
                            text: translations.i18n("Remove unlocking method")
                            visible: methodRow.managed && !methodRow.automatic
                            enabled: (root.credentialModel?.credentials.length ?? 0) > 1 && !root.credentialModel.busy
                            onTriggered: {
                                removeDialog.credentialId = methodRow.credentialId;
                                removeDialog.credentialLabel = methodRow.label;
                                removeDialog.open();
                            }
                        }
                    ]
                }
            }
        }

        footer: ColumnLayout {
            width: methodList.width
            spacing: 0

            Item {
                objectName: "unlockingMethodsProgress"
                Layout.fillWidth: true
                implicitHeight: progressRow.implicitHeight + Kirigami.Units.largeSpacing * 2
                visible: root.credentialModel?.busy ?? false

                RowLayout {
                    id: progressRow
                    anchors.centerIn: parent
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.BusyIndicator {
                        objectName: "unlockingMethodsBusyIndicator"
                        running: parent.parent.visible
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: implicitWidth
                    }
                    QQC2.Label {
                        objectName: "unlockingMethodsProgressText"
                        text: translations.i18n("Updating unlocking methods…")
                    }
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: methodList.count > 0 && (root.credentialModel?.errorMessage?.length ?? 0) > 0
                type: Kirigami.MessageType.Error
                text: root.credentialModel?.errorMessage ?? ""
                showCloseButton: true
                onVisibleChanged: if (!visible) root.credentialModel?.clearError()
            }
        }

        Kirigami.PlaceholderMessage {
            id: emptyMessage
            objectName: "emptyUnlockingMessage"
            width: parent.width - Kirigami.Units.largeSpacing * 4
            anchors.horizontalCenter: parent.horizontalCenter
            y: (methodList.headerItem?.height ?? 0) + Kirigami.Units.largeSpacing * 2
            visible: methodList.count === 0 && !(root.credentialModel?.busy ?? false)
            icon.name: (root.credentialModel?.errorMessage?.length ?? 0) > 0
                ? "dialog-error-symbolic"
                : "lock-symbolic"
            text: (root.credentialModel?.errorMessage?.length ?? 0) > 0
                ? root.credentialModel.errorMessage
                : translations.i18n("No LUKS unlocking methods found")
        }
    }

    AddPassphraseDialog {
        id: passphraseDialog
        credentialModel: root.credentialModel
    }

    GenerateKeyDialog {
        id: generateKeyDialog
        credentialModel: root.credentialModel
    }

    RemoveCredentialDialog {
        id: removeDialog
        credentialModel: root.credentialModel
    }

    ImportKeyDialog {
        id: importKeyDialog
        credentialModel: root.credentialModel
    }

    Component.onCompleted: {
        root.synchronizeCredentials()
        if (root.credentialModel !== null && root.profileId.length > 0)
            root.credentialModel.load(root.profileId)
    }

    function synchronizeCredentials() {
        stableCredentials.clear()
        const credentials = root.currentCredentials
        for (let index = 0; index < credentials.length; ++index) {
            const credential = credentials[index]
            stableCredentials.append({
                credentialId: String(credential.id ?? ""),
                label: String(credential.label ?? ""),
                credentialType: String(credential.type ?? ""),
                keyslot: Number(credential.keyslot ?? -1),
                managed: Boolean(credential.managed ?? false),
                automatic: Boolean(credential.automatic ?? false)
            })
        }
    }

    function methodDescription(method) {
        const kind = root.methodType(method)
        const ownership = method.managed ? translations.i18n("managed by btrfs-backup") : translations.i18n("configured outside btrfs-backup")
        const automatic = method.automatic ? translations.i18n("automatic backups") : translations.i18n("manual unlocking")
        return translations.i18nc("unlocking method details", "%1 - slot %2 - %3 - %4", kind, method.keyslot, ownership, automatic)
    }

    function methodType(method) {
        if (method === null || method === undefined)
            return translations.i18n("Unknown unlocking method")
        const credentialType = method.credentialType ?? method.type ?? ""
        if (credentialType === "passphrase")
            return translations.i18n("Passphrase")
        if (credentialType === "keyFile")
            return translations.i18n("Key file")
        return translations.i18n("Unknown unlocking method")
    }

    function methodIcon(method) {
        const credentialType = method?.credentialType ?? method?.type ?? ""
        if (credentialType === "passphrase")
            return "dialog-password";
        if (credentialType === "keyFile" && method?.automatic)
            return "password-generate";
        if (credentialType === "keyFile")
            return "document-encrypt";
        return "dialog-question";
    }

}
