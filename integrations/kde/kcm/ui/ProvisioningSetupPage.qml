// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

QQC2.ScrollView {
    id: root

    required property var workflow
    required property var translations
    readonly property alias profileName: configurationForm.profileName
    readonly property alias profileIdentifier: configurationForm.profileIdentifier
    readonly property alias profileIdentifierAcceptable: configurationForm.profileIdentifierAcceptable
    readonly property alias sourceCurrentIndex: configurationForm.sourceCurrentIndex
    readonly property alias sourceCurrentValue: configurationForm.sourceCurrentValue
    readonly property alias passphrase: configurationForm.passphrase
    readonly property alias confirmation: configurationForm.confirmation
    readonly property alias automaticKey: configurationForm.automaticKey
    readonly property alias eraseConfirmation: configurationForm.eraseConfirmation

    objectName: "newProfileStoragePage"
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: root.workflow.provisioning.errorMessage.length > 0
            text: root.workflow.provisioning.errorMessage
            showCloseButton: true
            onVisibleChanged: if (!visible) root.workflow.provisioning.clearError()
        }

        StorageTargetSelection {
            Layout.fillWidth: true
            workflow: root.workflow
            translations: root.translations
        }

        ProvisioningPlanPreview {
            workflow: root.workflow
            translations: root.translations
        }

        ProvisioningConfigurationForm {
            id: configurationForm
            workflow: root.workflow
            translations: root.translations
        }
    }
}
